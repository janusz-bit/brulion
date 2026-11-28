// The notebook's settings in a note (settingsnote/settingsnote.h, docs/NOTEBOOK-SETTINGS.md): the note
// found by `brulion: settings`; its YAML keys as config.json's; merged over config.json key by key — a
// note's invalid value keeps config.json's, warned; the migration from config.json idempotent and
// keeping the note's own values and text; one key written in place (CRLF, other lines byte for
// byte); malformed YAML; Polish text.
#include "settingsnote/settingsnote.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::settingsnote;

namespace {

const QString Note = u"---\nbrulion: settings\nweekStartsOn: 0\ndefaultReminderTime: \"08:30\"\nflashcards:\n"
                     u"  tags: [fiszki, słówka]\n  clozeBold: true\n---\n# Ustawienia notatnika\n\nTydzień od niedzieli.\n"_s;

} // namespace

class TestSettingsNote : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void names()
    {
        QCOMPARE(noteNames(), (QStringList{u"Notebook settings.md"_s, u"Ustawienia notatnika.md"_s}));
        QCOMPARE(noteName(u"pl"), u"Ustawienia notatnika.md"_s);
        QCOMPARE(noteName(u"en"), u"Notebook settings.md"_s);
        QCOMPARE(noteName(u""), u"Notebook settings.md"_s);
    }

    void recognised()
    {
        QVERIFY(isSettingsNote(Note));
        QVERIFY(isSettingsNote(u"---\r\nbrulion: Settings\r\n---\r\n"_s));
        QVERIFY(!isSettingsNote(u"---\nbrulion: document\n---\n"_s));
        QVERIFY(!isSettingsNote(u"brulion: settings\n"_s));
        QVERIFY(!isSettingsNote(QString()));
    }

    void settingsOfTheNote()
    {
        const QJsonObject o = noteSettings(Note);
        QVERIFY(!o.contains(u"brulion"_s));
        QCOMPARE(o.value(u"weekStartsOn"_s).toInt(-1), 0);
        QCOMPARE(o.value(u"defaultReminderTime"_s).toString(), u"08:30"_s);
        QCOMPARE(o.value(u"flashcards"_s).toObject().value(u"tags"_s).toArray().size(), 2);
        QVERIFY(o.value(u"flashcards"_s).toObject().value(u"clozeBold"_s).toBool());
        // No frontmatter, malformed YAML: nothing.
        QCOMPARE(noteSettings(u"# Ustawienia\n"_s), QJsonObject());
        QVERIFY(noteSettings(u"---\nbrulion: settings\n: : [\n---\n"_s).value(u"journalsFolder"_s).isUndefined());
    }

    void noteOverConfigJson()
    {
        const QByteArray json = R"({"version": 1, "journalsFolder": "Dziennik", "weekStartsOn": 1,
                                    "jutroKlucz": "zostaje"})";
        const ParsedVaultConfig merged = mergedConfig(json, Note);
        QCOMPARE(merged.config.journalsFolder, u"Dziennik"_s);       // (config.json's: the note has none)
        QCOMPARE(merged.config.weekStartsOn, Qt::Sunday);             // (the note's)
        QCOMPARE(merged.config.defaultReminderTime, QTime(8, 30));
        QCOMPARE(merged.config.flashcards.tags, (QStringList{u"fiszki"_s, u"słówka"_s}));
        QVERIFY(merged.config.flashcards.clozeBold);
        QCOMPARE(merged.raw.value(u"jutroKlucz"_s).toString(), u"zostaje"_s);
        QVERIFY2(merged.warnings.isEmpty(), qPrintable(merged.warnings.join(u" | "_s)));
        // Without the note: config.json's alone; without either: the defaults.
        QCOMPARE(mergedConfig(json, QString()).config.weekStartsOn, Qt::Monday);
        QCOMPARE(mergedConfig(QByteArray(), QString()).config.journalsFolder, VaultConfig().journalsFolder);
        // A note that is no settings note is not read.
        QCOMPARE(mergedConfig(json, u"---\nweekStartsOn: 0\n---\n"_s).config.weekStartsOn, Qt::Monday);
    }

    void invalidValueOfTheNote()
    {
        const QByteArray json = R"({"weekStartsOn": 3, "journalFileFormat": "dd.MM.yyyy"})";
        const ParsedVaultConfig merged =
                mergedConfig(json, u"---\nbrulion: settings\nweekStartsOn: wtorek\njournalFileFormat: x\n"
                                   u"defaultReminderTime: \"07:15\"\n---\n"_s);
        // (config.json's kept, never the default: a typo in the note does not reset the notebook.)
        QCOMPARE(merged.config.weekStartsOn, Qt::Wednesday);
        QCOMPARE(merged.config.journalFileFormat, u"dd.MM.yyyy"_s);
        QCOMPARE(merged.config.defaultReminderTime, QTime(7, 15));
        QCOMPARE(merged.warnings.size(), 2);
        QVERIFY(merged.warnings.at(0).startsWith(u"Notebook settings: "_s));
        QVERIFY(merged.warnings.join(u' ').contains(u"weekStartsOn"_s));
    }

    void migration()
    {
        const QByteArray json = R"({"version": 1, "journalsFolder": "Dziennik", "weekStartsOn": 1,
                                    "templatesFolder": "templates", "flashcards": {"tags": ["fiszki"]}})";
        const QString note = migratedNote(json, QString(), u"pl");
        QVERIFY(isSettingsNote(note));
        QVERIFY(note.contains(u"# Ustawienia notatnika"_s));
        const QJsonObject o = noteSettings(note);
        QCOMPARE(o.value(u"journalsFolder"_s).toString(), u"Dziennik"_s);
        // (Only what differs from the defaults: Monday and "templates" are.)
        QVERIFY(!o.contains(u"weekStartsOn"_s));
        QVERIFY(!o.contains(u"templatesFolder"_s));
        QVERIFY(!o.contains(u"version"_s));
        QCOMPARE(o.value(u"flashcards"_s).toObject().value(u"tags"_s).toArray().at(0).toString(), u"fiszki"_s);
        QCOMPARE(mergedConfig(json, note).config.journalsFolder, u"Dziennik"_s);
        // Idempotent: again, nothing changes.
        QCOMPARE(migratedNote(json, note, u"pl"), note);
        // An English notebook's note.
        QVERIFY(migratedNote(json, QString(), u"en").contains(u"# Notebook settings"_s));
    }

    void migrationKeepsTheNote()
    {
        const QByteArray json = R"({"journalsFolder": "Dziennik", "weekStartsOn": 0})";
        const QString note = u"---\r\nbrulion: settings\r\nweekStartsOn: 3\r\n---\r\nMoje notatki o ustawieniach.\r\n"_s;
        const QString migrated = migratedNote(json, note, u"pl");
        // (Its own value of weekStartsOn kept; journalsFolder added; its text and line endings kept.)
        QCOMPARE(noteSettings(migrated).value(u"weekStartsOn"_s).toInt(), 3);
        QCOMPARE(noteSettings(migrated).value(u"journalsFolder"_s).toString(), u"Dziennik"_s);
        QVERIFY(migrated.endsWith(u"---\r\nMoje notatki o ustawieniach.\r\n"_s));
        QVERIFY(migrated.contains(u"journalsFolder: Dziennik\r\n"_s));
        QCOMPARE(migratedNote(json, migrated, u"pl"), migrated);
    }

    void oneSettingWritten()
    {
        QString note = withSetting(Note, u"journalsFolder"_s, QJsonValue(u"Dziennik"_s));
        QCOMPARE(noteSettings(note).value(u"journalsFolder"_s).toString(), u"Dziennik"_s);
        QVERIFY(note.endsWith(u"---\n# Ustawienia notatnika\n\nTydzień od niedzieli.\n"_s));
        note = withSetting(note, u"weekStartsOn"_s, QJsonValue(2));
        QCOMPARE(noteSettings(note).value(u"weekStartsOn"_s).toInt(), 2);
        QCOMPARE(note.count(u"weekStartsOn"_s), 1);
        note = withSetting(note, u"flashcards"_s, QJsonObject{{u"tags"_s, QJsonArray{u"żółw"_s}}});
        QCOMPARE(noteSettings(note).value(u"flashcards"_s).toObject().value(u"tags"_s).toArray().at(0).toString(),
                 u"żółw"_s);
        note = withSetting(note, u"weekStartsOn"_s, QJsonValue(QJsonValue::Undefined));
        QVERIFY(!noteSettings(note).contains(u"weekStartsOn"_s));
        QVERIFY(isSettingsNote(note));
    }
};

QTEST_GUILESS_MAIN(TestSettingsNote)
#include "tst_settingsnote.moc"
