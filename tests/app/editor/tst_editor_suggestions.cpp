// SuggestionModel (src/app/editor/suggestionmodel.h): pages, blocks, tags, slash commands, dates
// and templates — Polish text, diacritic-insensitive filtering, the "create page" item and the
// natural-language date suggestions with a fixed clock.
#include "fakeeditorhost.h"
#include "suggestionmodel.h"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QTest>

using namespace brulion;
using namespace brulion::editor;
using namespace Qt::StringLiterals;

namespace {

QStringList titles(const SuggestionModel &m)
{
    QStringList out;
    for (int i = 0; i < m.count(); ++i)
        out.append(m.get(i).value(u"title"_s).toString());
    return out;
}

QStringList values(const SuggestionModel &m, const QString &key)
{
    QStringList out;
    for (int i = 0; i < m.count(); ++i)
        out.append(m.get(i).value(key).toString());
    return out;
}

} // namespace

class TestSuggestions : public QObject {
    Q_OBJECT

private:
    FakeEditorHost m_host;

private Q_SLOTS:
    void initTestCase() { m_host.populateSample(); }

    void pages()
    {
        SuggestionModel m;
        QAbstractItemModelTester tester(&m, QAbstractItemModelTester::FailureReportingMode::QtTest);
        m.setHost(&m_host);
        m.setMode(u"pages"_s);
        m.setFilter(u"remont"_s);
        QCOMPARE(titles(m), QStringList({u"Remont kuchni"_s, u"Remont łazienki"_s, u"remont"_s}));
        QCOMPARE(m.get(2).value(u"kind"_s).toString(), u"create"_s);
        QCOMPARE(m.get(0).value(u"subtitle"_s).toString(), u"Projekty"_s);
        QCOMPARE(m.get(1).value(u"insertText"_s).toString(), u"Remont łazienki"_s);
        QCOMPARE(m.get(1).value(u"path"_s).toString(), u"Projekty/Remont łazienki.md"_s);
        // Without diacritics, any case.
        m.setFilter(u"LAZIENKI"_s);
        QCOMPARE(titles(m).first(), u"Remont łazienki"_s);
        // Aliases.
        m.setFilter(u"Łazienka"_s);
        const QVariantMap alias = m.get(0);
        QCOMPARE(alias.value(u"kind"_s).toString(), u"alias"_s);
        QCOMPARE(alias.value(u"insertText"_s).toString(), u"Remont łazienki|Łazienka"_s);
        // An exact match: no "create" item.
        m.setFilter(u"zakupy"_s);
        QVERIFY(!values(m, u"kind"_s).contains(u"create"_s));
        QCOMPARE(m.get(0).value(u"title"_s).toString(), u"Zakupy"_s);
        // Templates are not pages.
        m.setFilter(u"Spotkanie"_s);
        QVERIFY(!values(m, u"path"_s).contains(u"templates/Spotkanie.md"_s));
        // Limit.
        m.setLimit(2);
        m.setFilter(QString());
        QCOMPARE(m.count(), 2);
    }

    void blocksAndTags()
    {
        SuggestionModel m;
        m.setHost(&m_host);
        m.setMode(u"blocks"_s);
        m.setFilter(u"plytki"_s);
        QVERIFY(m.count() >= 1);
        const QVariantMap first = m.get(0);
        QVERIFY(first.value(u"title"_s).toString().contains(u"płytki"_s, Qt::CaseInsensitive));
        QCOMPARE(first.value(u"subtitle"_s).toString(), u"Remont łazienki"_s);
        QVERIFY(first.value(u"line"_s).toInt() > 0);
        m.setFilter(u"hydraulik"_s);
        bool withId = false;
        for (int i = 0; i < m.count(); ++i)
            withId = withId || m.get(i).value(u"blockId"_s).toString() == u"hydr"_s;
        QVERIFY(withId);

        m.setMode(u"tags"_s);
        m.setFilter(u"zak"_s);
        QCOMPARE(m.get(0).value(u"title"_s).toString(), u"#zakupy"_s);
        QCOMPARE(m.get(0).value(u"insertText"_s).toString(), u"zakupy"_s);
        QVERIFY(!m.get(0).value(u"subtitle"_s).toString().isEmpty());
        m.setFilter(u"ogrod"_s);
        QCOMPARE(titles(m), QStringList({u"#ogród"_s}));
        m.setFilter(u"zakupy"_s); // typed completely: nothing left to complete
        QVERIFY(!titles(m).contains(u"#zakupy"_s));
    }

    void slash()
    {
        SuggestionModel m;
        QSignalSpy count(&m, &SuggestionModel::countChanged);
        m.setTrigger({{u"mode"_s, u"slash"_s}, {u"filter"_s, u"przyp"_s}, {u"token"_s, u"/"_s}});
        QCOMPARE(m.get(0).value(u"command"_s).toString(), u"reminder"_s);
        QCOMPARE(m.get(0).value(u"title"_s).toString(), u"Przypomnienie…"_s);
        QCOMPARE(m.get(0).value(u"subtitle"_s).toString(), u"Reminder…"_s);
        QCOMPARE(m.get(0).value(u"hint"_s).toString(), u"⏰"_s);
        QCOMPARE(m.get(0).value(u"icon"_s).toString(), u"bell"_s);
        m.setLocale(u"en"_s);
        QCOMPARE(m.get(0).value(u"title"_s).toString(), u"Reminder…"_s);
        m.setLocale(u"pl"_s);
        m.setFilter(u"naglowek"_s);
        QCOMPARE(values(m, u"command"_s).mid(0, 3), QStringList({u"h1"_s, u"h2"_s, u"h3"_s}));
        m.setFilter(u"zażółć"_s);
        QCOMPARE(m.count(), 0);
        m.setFilter(QString());
        QCOMPARE(m.count(), m.limit());
        QVERIFY(count.size() >= 2);
    }

    void dates()
    {
        SuggestionModel m;
        m.setHost(&m_host);
        m.setNow(u"2026-10-15 10:00"_s); // Thursday
        m.setTrigger({{u"mode"_s, u"date"_s}, {u"filter"_s, u"jutro o 9"_s}, {u"token"_s, u"⏰"_s}});
        QCOMPARE(m.count(), 1);
        QCOMPARE(m.get(0).value(u"insertText"_s).toString(), u"2026-10-16 09:00"_s);
        QVERIFY(m.get(0).value(u"subtitle"_s).toString().contains(u"jutro"_s));
        // Polish without diacritics, weekdays, English phrases.
        m.setFilter(u"w srode 18:30"_s);
        QCOMPARE(m.get(0).value(u"insertText"_s).toString(), u"2026-10-21 18:30"_s);
        m.setFilter(u"next friday 6pm"_s);
        QCOMPARE(m.get(0).value(u"insertText"_s).toString(), u"2026-10-23 18:00"_s);
        // Reminders get the default time when the phrase has none.
        m.setFilter(u"pojutrze"_s);
        QCOMPARE(m.get(0).value(u"insertText"_s).toString(), u"2026-10-17 09:00"_s);
        // Due dates stay dates.
        m.setToken(u"📅"_s);
        QCOMPARE(m.get(0).value(u"insertText"_s).toString(), u"2026-10-17"_s);
        // Unfinished phrases: matching suggestions.
        m.setFilter(u"ju"_s);
        QCOMPARE(m.get(0).value(u"hint"_s).toString(), u"jutro"_s);
        m.setFilter(QString());
        QVERIFY(m.count() >= 4);
        QCOMPARE(m.get(0).value(u"insertText"_s).toString(), u"2026-10-15"_s);
        m.setFilter(u"bez sensu"_s);
        QCOMPARE(m.count(), 0);
        // "@" offers the journal link, the date and a reminder.
        m.setTrigger({{u"mode"_s, u"date"_s}, {u"filter"_s, u"jutro o 9"_s}, {u"token"_s, u"@"_s}});
        QCOMPARE(values(m, u"kind"_s), QStringList({u"journal"_s, u"date"_s, u"reminder"_s}));
        QCOMPARE(m.get(0).value(u"insertText"_s).toString(), u"[[2026-10-16]]"_s);
        QCOMPARE(m.get(1).value(u"insertText"_s).toString(), u"📅 2026-10-16 09:00"_s);
        QCOMPARE(m.get(2).value(u"insertText"_s).toString(), u"⏰ 2026-10-16 09:00"_s);
        m.setFilter(QString());
        QCOMPARE(m.count(), 0);
    }

    void templates()
    {
        SuggestionModel m;
        m.setHost(&m_host);
        m.setMode(u"templates"_s);
        QCOMPARE(titles(m), QStringList({u"Spotkanie"_s, u"Zakupy tygodniowe"_s}));
        m.setFilter(u"tygod"_s);
        QCOMPARE(titles(m), QStringList({u"Zakupy tygodniowe"_s}));
        QCOMPARE(m.get(0).value(u"path"_s).toString(), u"templates/Zakupy tygodniowe.md"_s);
    }

    void detectAndInvalid()
    {
        SuggestionModel m;
        QCOMPARE(m.detect(u"tekst [[Rem"_s, 11).value(u"mode"_s).toString(), u"pages"_s);
        QCOMPARE(m.detect(u"tekst"_s, 5).value(u"mode"_s).toString(), QString());
        QVERIFY(m.get(-1).isEmpty());
        QVERIFY(m.get(99).isEmpty());
        m.setMode(u"pages"_s); // no host
        QCOMPARE(m.count(), 0);
        m.setMode(u"nonsense"_s);
        QCOMPARE(m.count(), 0);
    }

    void refreshesOnIndexChange()
    {
        FakeEditorHost host;
        host.setFile(u"A.md"_s, u"- x #alfa\n"_s);
        SuggestionModel m;
        m.setHost(&host);
        m.setMode(u"tags"_s);
        QCOMPARE(titles(m), QStringList({u"#alfa"_s}));
        host.setFile(u"B.md"_s, u"- y #beta\n"_s);
        Q_EMIT host.indexChanged();
        QCOMPARE(titles(m).size(), 2);
    }
};

QTEST_MAIN(TestSuggestions)
#include "tst_editor_suggestions.moc"
