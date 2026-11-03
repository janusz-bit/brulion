// Weekly and monthly notes in a vault (notebook/periodicnotes.h): where a period's note is (the
// configured folder, another letter case of it, a file that just arrived), its text from the
// period's template ({{date}} its first day, {{end}} its last, {{week}}, {{title}}, {{month}}, in Polish
// or English), created once and never over a note another program wrote, without a template empty,
// an invalid date nothing; the files cleanup never proposes.
#include "notebook/periodicnotes.h"

#include "filesystemtraits.h"
#include "index/vaultindex.h"
#include "notebook/cleanup.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::notebook;
using periodic::Period;

Q_DECLARE_METATYPE(brulion::periodic::Period)

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

class TestNotebookPeriodic : public QObject {
    Q_OBJECT
    QTemporaryDir m_tmp;
    int m_n = 0;
    const QDateTime m_now{QDate(2026, 11, 3), QTime(10, 30)};

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
    void fromTheTemplate_data()
    {
        QTest::addColumn<Period>("period");
        QTest::addColumn<QDate>("date");
        QTest::addColumn<int>("language"); // 0 Polish, 1 English
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("text");
        QTest::newRow("week") << Period::Week << QDate(2026, 11, 5) << 0 << u"journals/2026-W45.md"_s
                              << u"# Tydzień 45: 2.11–8.11.2026 (2026-W45)\n- \n"_s;
        QTest::newRow("week 53 across the year") << Period::Week << QDate(2027, 1, 2) << 0 << u"journals/2026-W53.md"_s
                                                 << u"# Tydzień 53: 28.12–3.01.2027 (2026-W53)\n- \n"_s;
        QTest::newRow("month") << Period::Month << QDate(2026, 2, 14) << 0 << u"journals/2026-02.md"_s
                               << u"## luty 2026: 1–28 (niedziela)\n- \n"_s;
        QTest::newRow("month in English") << Period::Month << QDate(2024, 2, 14) << 1 << u"journals/2024-02.md"_s
                                          << u"## February 2024: 1–29 (Thursday)\n- \n"_s;
    }
    void fromTheTemplate()
    {
        QFETCH(Period, period);
        QFETCH(QDate, date);
        QFETCH(int, language);
        QFETCH(QString, path);
        QFETCH(QString, text);
        QString root;
        auto vault = open(
                {{u"templates/weekly.md"_s, "# Tydzień {{week}}: {{date:d.MM}}–{{end:d.MM.yyyy}} ({{title}})\n- \n"},
                 {u"templates/monthly.md"_s, "## {{month}} {{date:yyyy}}: {{date:d}}–{{end:d}} ({{weekday}})\n- \n"}},
                &root);
        QVERIFY(vault);
        const dates::UiLocale locale = language == 0 ? dates::UiLocale::Polish : dates::UiLocale::English;
        QCOMPARE(periodicFileFor(*vault, period, date), path);
        QVERIFY(existingPeriodicNote(*vault, period, date).isEmpty());
        QCOMPARE(newPeriodicText(*vault, period, date, m_now, locale), text);
        const EnsuredJournal note = ensurePeriodicNote(*vault, period, date, m_now, locale);
        QCOMPARE(note.path, path);
        QVERIFY(note.created);
        QVERIFY(note.error.isEmpty());
        QCOMPARE(QString::fromUtf8(readRaw(root + u'/' + path)), text);
        // Again (any day of the period): it exists, nothing written.
        writeBehind(root + u'/' + path, "- moje\n");
        const EnsuredJournal again =
                ensurePeriodicNote(*vault, period, periodic::periodEnd(period, date), m_now, locale);
        QCOMPARE(again.path, path);
        QVERIFY(!again.created);
        QCOMPARE(readRaw(root + u'/' + path), "- moje\n"_ba);
        QCOMPARE(existingPeriodicNote(*vault, period, date), path);
    }

    void withoutTemplateEmpty()
    {
        QString root;
        auto vault = open({}, &root);
        QVERIFY(vault);
        QCOMPARE(newPeriodicText(*vault, Period::Week, QDate(2026, 11, 3), m_now, dates::UiLocale::Polish), QString());
        const EnsuredJournal note =
                ensurePeriodicNote(*vault, Period::Month, QDate(2026, 11, 3), m_now, dates::UiLocale::Polish);
        QCOMPARE(note.path, u"journals/2026-11.md"_s);
        QVERIFY(note.created);
        QVERIFY(QFileInfo::exists(root + u"/journals/2026-11.md"_s));
        QCOMPARE(readRaw(root + u"/journals/2026-11.md"_s), QByteArray());
        // An invalid date: nothing.
        QVERIFY(periodicFileFor(*vault, Period::Week, QDate()).isEmpty());
        const EnsuredJournal none = ensurePeriodicNote(*vault, Period::Week, QDate(), m_now, dates::UiLocale::Polish);
        QVERIFY(none.path.isEmpty());
        QVERIFY(!none.created);
    }

    void configuredFolderAndFormat()
    {
        QString root;
        auto vault = open({{u".brulion/config.json"_s,
                            R"({"journalsFolder": "Dziennik", "weeklyFolder": "Dziennik/Tygodnie",)"
                            R"( "weeklyFileFormat": "yyyy/'Tydzień' ww", "monthlyFileFormat": "MMMM yyyy",)"
                            R"( "weeklyTemplate": "szablony/tydzień.md"})"},
                           {u"szablony/tydzień.md"_s, "Od {{date:dddd d MMMM}} do {{end:dddd d MMMM}}\n"}},
                          &root);
        QVERIFY(vault);
        QCOMPARE(periodicFileFor(*vault, Period::Week, QDate(2026, 11, 3)), u"Dziennik/Tygodnie/2026/Tydzień 45.md"_s);
        QCOMPARE(newPeriodicText(*vault, Period::Week, QDate(2026, 11, 3), m_now, dates::UiLocale::Polish),
                 u"Od poniedziałek 2 listopada do niedziela 8 listopada\n"_s);
        // The monthly notes follow the journals folder; the default template is not there.
        QCOMPARE(periodicFileFor(*vault, Period::Month, QDate(2026, 11, 3)), u"Dziennik/November 2026.md"_s);
        QCOMPARE(newPeriodicText(*vault, Period::Month, QDate(2026, 11, 3), m_now, dates::UiLocale::Polish), QString());
        const EnsuredJournal week =
                ensurePeriodicNote(*vault, Period::Week, QDate(2026, 11, 8), m_now, dates::UiLocale::Polish);
        QVERIFY(week.created);
        QVERIFY(QFileInfo::exists(root + u"/Dziennik/Tygodnie/2026/Tydzień 45.md"_s));
    }

    void existingUnderOtherNames()
    {
        BRULION_SKIP_IF_CASE_INSENSITIVE(m_tmp.path()); // "journals/" next to "Journals/"
        QString root;
        // A Polish month name and a capitalised folder from another device: no second note.
        auto vault = open({{u".brulion/config.json"_s, R"({"monthlyFileFormat": "MMMM yyyy"})"},
                           {u"Journals/listopad 2026.md"_s, "- z innego urządzenia\n"},
                           {u"Journals/2026-W45.md"_s, "- tydzień\n"}},
                          &root);
        QVERIFY(vault);
        QCOMPARE(existingPeriodicNote(*vault, Period::Month, QDate(2026, 11, 20)), u"Journals/listopad 2026.md"_s);
        QCOMPARE(periodicFileFor(*vault, Period::Month, QDate(2026, 11, 20)), u"Journals/listopad 2026.md"_s);
        QCOMPARE(periodicFileFor(*vault, Period::Week, QDate(2026, 11, 2)), u"Journals/2026-W45.md"_s);
        const EnsuredJournal note =
                ensurePeriodicNote(*vault, Period::Month, QDate(2026, 11, 1), m_now, dates::UiLocale::Polish);
        QCOMPARE(note.path, u"Journals/listopad 2026.md"_s);
        QVERIFY(!note.created);
        // The configured spelling wins when both are there.
        writeBehind(root + u"/journals/2026-W45.md"_s, "- tu\n");
        vault->rescan();
        vault->processPendingEvents();
        QCOMPARE(existingPeriodicNote(*vault, Period::Week, QDate(2026, 11, 2)), u"journals/2026-W45.md"_s);
    }

    void fileThatJustArrived()
    {
        QString root;
        auto vault = open({}, &root);
        QVERIFY(vault);
        // Synced in a moment ago: the vault has not listed it yet, it is not written over.
        writeBehind(root + u"/journals/2026-W45.md"_s, "- z Syncthinga\n");
        QCOMPARE(periodicFileFor(*vault, Period::Week, QDate(2026, 11, 3)), u"journals/2026-W45.md"_s);
        const EnsuredJournal note =
                ensurePeriodicNote(*vault, Period::Week, QDate(2026, 11, 3), m_now, dates::UiLocale::Polish);
        QVERIFY(!note.created);
        QCOMPARE(readRaw(root + u"/journals/2026-W45.md"_s), "- z Syncthinga\n"_ba);
    }

    void cleanupKeepsThem()
    {
        // Weekly and monthly notes and their templates are no files "nothing refers to".
        VaultIndex index;
        VaultConfig config;
        config.templatesFolder = u"szablony"_s; // the templates themselves are outside it
        index.setConfig(config);
        for (const QString &path : {u"journals/2026-W45.md"_s, u"journals/2026-11.md"_s, u"templates/weekly.md"_s,
                                    u"templates/monthly.md"_s, u"Samotna.md"_s})
            index.update(path, u"- tekst\n"_s);
        QStringList left;
        for (const UnreferencedFile &f : unreferencedFiles(index, config))
            left.append(f.path);
        QCOMPARE(left, QStringList{u"Samotna.md"_s});
    }
};

QTEST_GUILESS_MAIN(TestNotebookPeriodic)
#include "tst_notebook_periodic.moc"
