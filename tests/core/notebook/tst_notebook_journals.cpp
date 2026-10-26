// Daily notes (notebook/journals.h): appending a block keeps every other byte (line endings, a
// BOM, no final newline) and fills a template's empty last bullet; free text becomes one block;
// the note of a day is found under any name the journal folder accepts and created from the
// journal template (in Polish or English, with the configured file name format; newJournalText()
// without writing), never over a note another program created a moment ago; the app's language
// for its saved setting.
#include "notebook/journals.h"

#include "filesystemtraits.h"
#include "vault/vault.h"
#include "vault/vaultconfig.h"

#include <QDir>
#include <QFile>
#include <QLocale>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::notebook;

namespace {

void writeBehind(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(bytes);
    QVERIFY(f.commit());
}

QByteArray readRaw(const QString &file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class TestNotebookJournals : public QObject {
    Q_OBJECT
    QTemporaryDir m_tmp;
    int m_n = 0;
    const QDateTime m_now{QDate(2026, 10, 14), QTime(10, 30)};

    /// A vault with `files` and an optional config.json.
    std::unique_ptr<Vault> open(const QHash<QString, QByteArray> &files, QString *root = nullptr)
    {
        const QString dir = m_tmp.filePath(u"v%1"_s.arg(++m_n));
        QDir().mkpath(dir);
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            writeBehind(dir + u'/' + it.key(), it.value());
        auto vault = std::make_unique<Vault>(dir, m_tmp.filePath(u"s%1"_s.arg(m_n)));
        if (!vault->open())
            return nullptr;
        if (root)
            *root = dir;
        return vault;
    }

private Q_SLOTS:
    void appendBlock_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("block");
        QTest::addColumn<QString>("expected");
        QTest::newRow("empty note") << QString() << u"- [ ] Kupić mleko"_s << u"- [ ] Kupić mleko\n"_s;
        QTest::newRow("LF") << u"- a\n"_s << u"- b"_s << u"- a\n- b\n"_s;
        QTest::newRow("CRLF") << u"- a\r\n- b\r\n"_s << u"- c\n  dalej"_s << u"- a\r\n- b\r\n- c\r\n  dalej\r\n"_s;
        QTest::newRow("no final newline") << u"- a"_s << u"- b"_s << u"- a\n- b\n"_s;
        QTest::newRow("no final newline, CRLF") << u"- a\r\n- b"_s << u"- c"_s << u"- a\r\n- b\r\n- c\r\n"_s;
        QTest::newRow("trailing blank lines") << u"- a\n\n\n"_s << u"- b"_s << u"- a\n- b\n"_s;
        QTest::newRow("template's empty bullet") << u"## Plan dnia\n- \n"_s << u"- [ ] Zadzwonić do Łukasza"_s
                                                 << u"## Plan dnia\n- [ ] Zadzwonić do Łukasza\n"_s;
        QTest::newRow("empty bullet with CRLF")
                << u"# Środa\r\n-\r\n\r\n"_s << u"- notatka"_s << u"# Środa\r\n- notatka\r\n"_s;
        QTest::newRow("a BOM stays") << u"\uFEFF- a\n"_s << u"- ż"_s << u"\uFEFF- a\n- ż\n"_s;
        QTest::newRow("lone CR") << u"- a\r- b\r"_s << u"- c"_s << u"- a\r- b\r- c\r"_s;
        QTest::newRow("blank block") << u"- a\n"_s << u"  \n"_s << u"- a\n"_s;
        QTest::newRow("only blank lines") << u"\n\n"_s << u"- x"_s << u"- x\n"_s;
    }
    void appendBlock()
    {
        QFETCH(QString, content);
        QFETCH(QString, block);
        QFETCH(QString, expected);
        QCOMPARE(notebook::appendBlock(content, block), expected);
    }

    void blockFromText_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("block");
        QTest::newRow("one line") << u"Pomysł na prezent"_s << u"- Pomysł na prezent"_s;
        QTest::newRow("list marker kept") << u"* gwiazdka"_s << u"* gwiazdka"_s;
        QTest::newRow("numbered") << u"1. pierwszy"_s << u"1. pierwszy"_s;
        QTest::newRow("task") << u"- [ ] zadanie"_s << u"- [ ] zadanie"_s;
        QTest::newRow("more lines") << u"\n  Zakupy:\r\nmleko\n  chleb \n\n"_s << u"- Zakupy:\n  mleko\n  chleb"_s;
        QTest::newRow("blank") << u" \n\t\n"_s << QString();
        QTest::newRow("dash without space is text") << u"-5 stopni"_s << u"- -5 stopni"_s;
    }
    void blockFromText()
    {
        QFETCH(QString, text);
        QFETCH(QString, block);
        QCOMPARE(notebook::blockFromText(text), block);
    }

    void contentLocales()
    {
        QCOMPARE(notebook::contentLocale(u"en"), dates::UiLocale::English);
        QCOMPARE(notebook::contentLocale(u"pl"), dates::UiLocale::Polish);
        QCOMPARE(notebook::contentLocale(u""), dates::UiLocale::Polish);
        QCOMPARE(notebook::contentLocale(u"de"), dates::UiLocale::Polish);
    }

    // The app's language for its setting ui/language: as saved, else the system's.
    void appLanguages()
    {
        const QLocale system;
        QCOMPARE(notebook::appLanguage(u"pl"_s), u"pl"_s);
        QCOMPARE(notebook::appLanguage(u"en"_s), u"en"_s);
        QLocale::setDefault(QLocale(u"en_GB"_s));
        QCOMPARE(notebook::appLanguage(QString()), u"en"_s); // never chosen
        QCOMPARE(notebook::appLanguage(u"de"_s), u"en"_s);   // unknown
        QCOMPARE(notebook::appLanguage(u"pl"_s), u"pl"_s);
        QLocale::setDefault(QLocale(u"pl_PL"_s));
        QCOMPARE(notebook::appLanguage(QString()), u"pl"_s);
        QLocale::setDefault(QLocale(u"de_DE"_s));
        QCOMPARE(notebook::appLanguage(QString()), u"pl"_s);
        QLocale::setDefault(system);
    }

    // What a new daily note starts with (a dry run shows it): the template filled for its day.
    void newJournalText()
    {
        QString root;
        auto vault = open({{u"templates/journal.md"_s, "# {{weekday}} {{date}}\n- \n"}}, &root);
        QVERIFY(vault);
        QCOMPARE(notebook::newJournalText(*vault, QDate(2026, 10, 15), m_now, dates::UiLocale::Polish),
                 u"# czwartek 2026-10-15\n- \n"_s);
        QCOMPARE(notebook::newJournalText(*vault, QDate(2026, 10, 15), m_now, dates::UiLocale::English),
                 u"# Thursday 2026-10-15\n- \n"_s);
        QVERIFY(!QFileInfo::exists(root + u"/journals"_s)); // never writes
        auto plain = open({});
        QVERIFY(plain);
        QCOMPARE(notebook::newJournalText(*plain, QDate(2026, 10, 15), m_now, dates::UiLocale::Polish), QString());
        QCOMPARE(notebook::newJournalText(*vault, QDate(), m_now, dates::UiLocale::Polish), QString());
    }

    void ensureJournalFromTheTemplate_data()
    {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("expected");
        QTest::newRow("pl") << u"pl"_s << u"# środa 2026-10-14\n- \n"_s;
        QTest::newRow("en") << u"en"_s << u"# Wednesday 2026-10-14\n- \n"_s;
    }
    void ensureJournalFromTheTemplate()
    {
        QFETCH(QString, language);
        QFETCH(QString, expected);
        QString root;
        auto vault = open({{u"templates/journal.md"_s, "# {{weekday}} {{date}}\n- \n"}}, &root);
        QVERIFY(vault);
        const EnsuredJournal j = ensureJournal(*vault, QDate(2026, 10, 14), m_now, contentLocale(language));
        QCOMPARE(j.path, u"journals/2026-10-14.md"_s);
        QVERIFY(j.created);
        QCOMPARE(QString::fromUtf8(readRaw(root + u"/journals/2026-10-14.md"_s)), expected);
        // Again: it exists, nothing written.
        writeBehind(root + u"/journals/2026-10-14.md"_s, "- moje\n");
        const EnsuredJournal again = ensureJournal(*vault, QDate(2026, 10, 14), m_now, contentLocale(language));
        QCOMPARE(again.path, j.path);
        QVERIFY(!again.created);
        QCOMPARE(readRaw(root + u"/journals/2026-10-14.md"_s), "- moje\n"_ba);
    }

    void journalFileFormatAndFolder()
    {
        QString root;
        auto vault = open(
                {{u".brulion/config.json"_s, R"({"journalsFolder": "Dziennik", "journalFileFormat": "dd.MM.yyyy"})"},
                 {u"templates/journal.md"_s, "- {{date:dddd, d MMMM yyyy}}\n"}},
                &root);
        QVERIFY(vault);
        QCOMPARE(journalFileFor(*vault, QDate(2026, 10, 14)), u"Dziennik/14.10.2026.md"_s);
        const EnsuredJournal j = ensureJournal(*vault, QDate(2026, 10, 14), m_now, dates::UiLocale::Polish);
        QCOMPARE(j.path, u"Dziennik/14.10.2026.md"_s);
        QCOMPARE(QString::fromUtf8(readRaw(root + u"/Dziennik/14.10.2026.md"_s)), u"- środa, 14 października 2026\n"_s);
        QVERIFY(journalFileFor(*vault, QDate()).isEmpty());
    }

    void existingNotesUnderOtherNames()
    {
        BRULION_SKIP_IF_CASE_INSENSITIVE(m_tmp.path()); // "journals/" next to "Journals/"
        QString root;
        // Logseq's name and a capitalised folder from another device: no second note for the day.
        auto vault = open({{u"journals/2026_10_14.md"_s, "- z Logseqa\n"}, {u"Journals/2026-10-13.md"_s, "- inny\n"}},
                          &root);
        QVERIFY(vault);
        QCOMPARE(journalFileFor(*vault, QDate(2026, 10, 14)), u"journals/2026_10_14.md"_s);
        QCOMPARE(existingJournal(*vault, QDate(2026, 10, 14)), u"journals/2026_10_14.md"_s);
        const EnsuredJournal j = ensureJournal(*vault, QDate(2026, 10, 14), m_now, dates::UiLocale::Polish);
        QCOMPARE(j.path, u"journals/2026_10_14.md"_s);
        QVERIFY(!j.created);
        QCOMPARE(readRaw(root + u"/journals/2026_10_14.md"_s), "- z Logseqa\n"_ba);
        QCOMPARE(journalFileFor(*vault, QDate(2026, 10, 13)), u"Journals/2026-10-13.md"_s);
        QVERIFY(existingJournal(*vault, QDate(2026, 10, 12)).isEmpty());
        QCOMPARE(journalFileFor(*vault, QDate(2026, 10, 12)), u"journals/2026-10-12.md"_s);
    }

    void neverOverAJournalCreatedMeanwhile()
    {
        QString root;
        auto vault = open({{u"templates/journal.md"_s, "## Plan\n- \n"}}, &root);
        QVERIFY(vault);
        // Created on disk by another program after the vault listed the folder: kept.
        writeBehind(root + u"/journals/2026-10-15.md"_s, "- z telefonu\n");
        const EnsuredJournal j = ensureJournal(*vault, QDate(2026, 10, 15), m_now, dates::UiLocale::Polish);
        QCOMPARE(j.path, u"journals/2026-10-15.md"_s);
        QVERIFY(!j.created);
        QCOMPARE(readRaw(root + u"/journals/2026-10-15.md"_s), "- z telefonu\n"_ba);
        // Without a template the note starts empty.
        auto bare = open({});
        QVERIFY(bare);
        const EnsuredJournal empty = ensureJournal(*bare, QDate(2026, 10, 15), m_now, dates::UiLocale::Polish);
        QVERIFY(empty.created);
        QCOMPARE(bare->readText(empty.path), QString());
    }

    void templateText()
    {
        auto vault = open({{u"templates/Spotkanie.md"_s, "# {{title}}\n- {{date}} {{time}}{{cursor}}\n"}});
        QVERIFY(vault);
        QCOMPARE(notebook::templateText(*vault, u"templates/Spotkanie.md"_s, m_now, u"Zebranie"_s,
                                        dates::UiLocale::Polish),
                 u"# Zebranie\n- 2026-10-14 10:30\n"_s);
        QVERIFY(notebook::templateText(*vault, u"templates/brak.md"_s, m_now, u"x"_s, dates::UiLocale::Polish)
                        .isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestNotebookJournals)
#include "tst_notebook_journals.moc"
