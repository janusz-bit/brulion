// SuggestionModel (src/app/editor/suggestionmodel.h): pages, blocks, tags, slash commands, dates
// and templates — Polish text, diacritic-insensitive filtering, the "create page" item and the
// natural-language date suggestions with a fixed clock.
#include "blocktext.h"
#include "fakeeditorhost.h"
#include "suggestionmodel.h"

#include <QAbstractItemModelTester>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlIncubationController>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

#include <memory>

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

/// A host counting what the popup asks of the index.
class CountingHost : public FakeEditorHost {
public:
    mutable int pageAsks = 0;
    mutable int blockAsks = 0;
    QVariantList suggestPages(const QString &prefix, int limit) const override
    {
        ++pageAsks;
        return FakeEditorHost::suggestPages(prefix, limit);
    }
    QVariantList suggestBlocks(const QString &text, int limit) const override
    {
        ++blockAsks;
        return FakeEditorHost::suggestBlocks(text, limit);
    }
};

/// Incubates asynchronous QML objects only when told to: the delegates a list view builds ahead
/// of its viewport stay half-made until then, as on a slow phone between two keystrokes.
class HeldIncubation : public QQmlIncubationController {
public:
    void finish()
    {
        for (int i = 0; i < 100 && incubatingObjectCount() > 0; ++i)
            incubateFor(20);
    }
};

/// The text of the delegate in row `row` of a ListView (itemAtIndex), or "" without one.
QString rowText(QQuickItem *view, int row)
{
    QQuickItem *item = nullptr;
    QMetaObject::invokeMethod(view, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, row));
    return item ? item->property("text").toString() : QString();
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
        // "Create page" first (always in view), the best existing match highlighted.
        QCOMPARE(titles(m), QStringList({u"remont"_s, u"Remont kuchni"_s, u"Remont łazienki"_s}));
        QCOMPARE(m.get(0).value(u"kind"_s).toString(), u"create"_s);
        QCOMPARE(m.defaultIndex(), 1);
        QCOMPARE(m.get(1).value(u"subtitle"_s).toString(), u"Projekty"_s);
        QCOMPARE(m.get(2).value(u"insertText"_s).toString(), u"Remont łazienki"_s);
        QCOMPARE(m.get(2).value(u"path"_s).toString(), u"Projekty/Remont łazienki.md"_s);
        // Without diacritics, any case.
        m.setFilter(u"LAZIENKI"_s);
        QCOMPARE(titles(m).at(m.defaultIndex()), u"Remont łazienki"_s);
        // Aliases.
        m.setFilter(u"Łazienka"_s);
        const QVariantMap alias = m.get(m.defaultIndex());
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

    // `[[`: "Create page" is the first row whenever the typed name is no page or alias yet (so a
    // long list never hides it), but Enter keeps taking the best existing match (defaultIndex).
    void createPageItem_data()
    {
        QTest::addColumn<QString>("filter");
        QTest::addColumn<QStringList>("expectedTitles");
        QTest::addColumn<QStringList>("expectedKinds");
        QTest::addColumn<int>("expectedDefault");

        QTest::newRow("prefix of a page") << u"Proj"_s << QStringList{u"Proj"_s, u"Projekty"_s}
                                          << QStringList{u"create"_s, u"page"_s} << 1;
        QTest::newRow("trimmed") << u"  Proj "_s << QStringList{u"Proj"_s, u"Projekty"_s}
                                 << QStringList{u"create"_s, u"page"_s} << 1;
        QTest::newRow("no match") << u"Nowa strona ąę"_s << QStringList{u"Nowa strona ąę"_s} << QStringList{u"create"_s}
                                  << 0;
        QTest::newRow("exact, other case") << u"zakupy"_s << QStringList{u"Zakupy"_s} << QStringList{u"page"_s} << 0;
        QTest::newRow("exact, upper case") << u"ZAKUPY"_s << QStringList{u"Zakupy"_s} << QStringList{u"page"_s} << 0;
        QTest::newRow("exact, diacritics") << u"Łódź"_s << QStringList{u"Łódź"_s} << QStringList{u"page"_s} << 0;
        QTest::newRow("exact, without diacritics") << u"lodz"_s << QStringList{u"Łódź"_s} << QStringList{u"page"_s} << 0;
        QTest::newRow("prefix, without diacritics") << u"lod"_s << QStringList{u"lod"_s, u"Łódź"_s}
                                                    << QStringList{u"create"_s, u"page"_s} << 1;
        QTest::newRow("exact alias") << u"telefony"_s << QStringList{u"Telefony"_s} << QStringList{u"alias"_s} << 0;
        QTest::newRow("alias prefix") << u"Tele"_s << QStringList{u"Tele"_s, u"Telefony"_s}
                                      << QStringList{u"create"_s, u"alias"_s} << 1;
        QTest::newRow("nothing typed") << QString() << QStringList{u"Kontakty"_s, u"Projekty"_s, u"Zakupy"_s, u"Łódź"_s}
                                       << QStringList{u"page"_s, u"page"_s, u"page"_s, u"page"_s} << 0;
    }

    void createPageItem()
    {
        QFETCH(QString, filter);
        QFETCH(QStringList, expectedTitles);
        QFETCH(QStringList, expectedKinds);
        QFETCH(int, expectedDefault);
        FakeEditorHost host;
        host.setFile(u"Projekty.md"_s, u"- plan\n"_s);
        host.setFile(u"Zakupy.md"_s, u"- [ ] Mleko\n"_s);
        host.setFile(u"Łódź.md"_s, u"- Piotrkowska\n"_s);
        host.setFile(u"Kontakty.md"_s, u"---\naliases: [Telefony]\n---\n- Hydraulik\n"_s);
        SuggestionModel m;
        QAbstractItemModelTester tester(&m, QAbstractItemModelTester::FailureReportingMode::QtTest);
        m.setHost(&host);
        m.setMode(u"pages"_s);
        m.setFilter(filter);
        QCOMPARE(titles(m), expectedTitles);
        QCOMPARE(values(m, u"kind"_s), expectedKinds);
        QCOMPARE(m.defaultIndex(), expectedDefault);
        if (expectedKinds.first() == u"create")
            QCOMPARE(m.get(0).value(u"insertText"_s).toString(), filter.trimmed());
    }

    // Many matching pages: the create row is still there (first), on top of `limit` pages; the
    // default index follows the model (defaultIndexChanged before the QML popup reads it).
    void createPageItemWithManyMatches()
    {
        FakeEditorHost host;
        for (int i = 1; i <= 20; ++i)
            host.setFile(u"Projekt %1.md"_s.arg(i, 2, 10, QLatin1Char('0')), QString());
        SuggestionModel m;
        m.setHost(&host);
        m.setMode(u"pages"_s);
        m.setLimit(12);
        QSignalSpy defaultSpy(&m, &SuggestionModel::defaultIndexChanged);
        int defaultAtReset = -1;
        connect(&m, &QAbstractItemModel::modelReset, this, [&] { defaultAtReset = m.defaultIndex(); });
        m.setFilter(u"Projekt"_s);
        QCOMPARE(m.count(), 13);
        QCOMPARE(m.get(0).value(u"kind"_s).toString(), u"create"_s);
        QCOMPARE(m.get(0).value(u"title"_s).toString(), u"Projekt"_s);
        QCOMPARE(m.get(1).value(u"title"_s).toString(), u"Projekt 01"_s);
        QCOMPARE(m.defaultIndex(), 1);
        QCOMPARE(defaultAtReset, 1);
        QCOMPARE(defaultSpy.count(), 1);
        // An exact name: no create row, the page first.
        m.setFilter(u"projekt 07"_s);
        QCOMPARE(titles(m), QStringList({u"Projekt 07"_s}));
        QCOMPARE(m.defaultIndex(), 0);
        QCOMPARE(defaultSpy.count(), 2);
        // Other modes never offer creation: the first row is the default.
        m.setMode(u"blocks"_s);
        QCOMPARE(m.defaultIndex(), 0);
        m.setMode(u"tags"_s);
        m.setFilter(u"Proj"_s);
        QVERIFY(m.count() > 0);
        QVERIFY(!values(m, u"kind"_s).contains(u"create"_s));
        QCOMPARE(m.defaultIndex(), 0);
        // A page of exactly that name ranked below the limit: still no create row.
        host.setFile(u"Projekt.md"_s, QString());
        m.setMode(u"pages"_s);
        m.setFilter(u"projekt"_s);
        QCOMPARE(m.count(), 12);
        QVERIFY(!values(m, u"kind"_s).contains(u"create"_s));
        QVERIFY(!values(m, u"title"_s).contains(u"Projekt"_s));
        QCOMPARE(m.defaultIndex(), 0);
    }

    // Other spellings of an existing page (its folder path, runs of spaces) are no new page.
    void createPageItemForExistingSpellings_data()
    {
        QTest::addColumn<QString>("filter");
        QTest::addColumn<QStringList>("expectedTitles");
        QTest::addColumn<QStringList>("expectedKinds");
        QTest::addColumn<int>("expectedDefault");

        QTest::newRow("folder path") << u"Projekty/Remont kuchni"_s << QStringList{u"Remont kuchni"_s}
                                     << QStringList{u"page"_s} << 0;
        QTest::newRow("folder path, other case") << u"projekty/REMONT kuchni"_s << QStringList{u"Remont kuchni"_s}
                                                 << QStringList{u"page"_s} << 0;
        QTest::newRow("folder path, without diacritics") << u"Miasta/Lodz"_s << QStringList{u"Łódź"_s}
                                                         << QStringList{u"page"_s} << 0;
        QTest::newRow("folder path prefix") << u"Projekty/Remont"_s << QStringList{u"Projekty/Remont"_s, u"Remont kuchni"_s}
                                            << QStringList{u"create"_s, u"page"_s} << 1;
        QTest::newRow("folder path, no page") << u"Projekty/Ogród"_s << QStringList{u"Projekty/Ogród"_s}
                                              << QStringList{u"create"_s} << 0;
        QTest::newRow("doubled space") << u"Remont  kuchni"_s << QStringList{u"Remont kuchni"_s} << QStringList{u"page"_s}
                                       << 0;
        QTest::newRow("tab and spaces") << u" remont \t kuchni "_s << QStringList{u"Remont kuchni"_s}
                                        << QStringList{u"page"_s} << 0;
        QTest::newRow("doubled space, prefix") << u"Remont  kuch"_s << QStringList{u"Remont  kuch"_s, u"Remont kuchni"_s}
                                               << QStringList{u"create"_s, u"page"_s} << 1;
    }

    void createPageItemForExistingSpellings()
    {
        QFETCH(QString, filter);
        QFETCH(QStringList, expectedTitles);
        QFETCH(QStringList, expectedKinds);
        QFETCH(int, expectedDefault);
        FakeEditorHost host;
        host.setFile(u"Projekty/Remont kuchni.md"_s, u"- płytki\n"_s);
        host.setFile(u"Miasta/Łódź.md"_s, QString());
        host.setFile(u"Notatka.md"_s, QString());
        SuggestionModel m;
        QAbstractItemModelTester tester(&m, QAbstractItemModelTester::FailureReportingMode::QtTest);
        m.setHost(&host);
        m.setMode(u"pages"_s);
        m.setCurrentPath(u"Notatka.md"_s);
        m.setFilter(filter);
        QCOMPARE(titles(m), expectedTitles);
        QCOMPARE(values(m, u"kind"_s), expectedKinds);
        QCOMPARE(m.defaultIndex(), expectedDefault);
        if (expectedKinds.first() == u"page")
            QCOMPARE(m.get(0).value(u"insertText"_s).toString(), expectedTitles.first());
    }

    // Two pages called "Dom": the one the bare title does not reach is linked by its path (the
    // folder is shown as the subtitle), from wherever the link is written.
    void pagesWithTheSameName()
    {
        FakeEditorHost host;
        host.setFile(u"Dom.md"_s, u"# Dom\n"_s);
        host.setFile(u"Projekty/Dom.md"_s, u"---\naliases: [Chata]\n---\n"_s);
        host.setFile(u"Notatka.md"_s, QString());
        SuggestionModel m;
        m.setHost(&host);
        m.setMode(u"pages"_s);
        m.setCurrentPath(u"Notatka.md"_s);
        m.setFilter(u"dom"_s);
        QHash<QString, QString> inserted;
        QHash<QString, QString> subtitles;
        for (int i = 0; i < m.count(); ++i) {
            inserted.insert(m.get(i).value(u"path"_s).toString(), m.get(i).value(u"insertText"_s).toString());
            subtitles.insert(m.get(i).value(u"path"_s).toString(), m.get(i).value(u"subtitle"_s).toString());
        }
        QCOMPARE(inserted.value(u"Dom.md"_s), u"Dom"_s);
        QCOMPARE(inserted.value(u"Projekty/Dom.md"_s), u"Projekty/Dom"_s);
        QCOMPARE(subtitles.value(u"Projekty/Dom.md"_s), u"Projekty"_s);
        // An alias keeps its text, the target names the page.
        m.setFilter(u"chata"_s);
        QCOMPARE(m.get(0).value(u"insertText"_s).toString(), u"Projekty/Dom|Chata"_s);
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

    // `#` offers tags and pages (a tag names a page, docs/SYNTAX.md §3): a tag naming a page is
    // one item; a page no tag names yet is offered with its folder.
    void tagsAndPages()
    {
        FakeEditorHost host;
        host.setFile(u"Żółw.md"_s, u"- zwierzę\n"_s);
        host.setFile(u"Ludzie/Jan Kowalski.md"_s, u"- tel.\n"_s);
        host.setFile(u"C++.md"_s, u"- język\n"_s);
        host.setFile(u"Notatki.md"_s, u"- #żółw i #praca\n"_s);
        SuggestionModel m;
        m.setHost(&host);
        m.setMode(u"tags"_s);
        m.setFilter(u"zolw"_s);
        QCOMPARE(titles(m), QStringList{u"#żółw"_s});
        QCOMPARE(m.get(0).value(u"path"_s).toString(), u"Żółw.md"_s);
        QCOMPARE(m.get(0).value(u"icon"_s).toString(), u"hash"_s);
        m.setFilter(u"jan"_s);
        QCOMPARE(titles(m), QStringList{u"#Jan Kowalski"_s});
        QCOMPARE(m.get(0).value(u"icon"_s).toString(), u"file-text"_s);
        QCOMPARE(m.get(0).value(u"subtitle"_s).toString(), u"Page in Ludzie"_s);
        QCOMPARE(m.get(0).value(u"insertText"_s).toString(), u"Jan Kowalski"_s);
        QCOMPARE(m.get(0).value(u"path"_s).toString(), u"Ludzie/Jan Kowalski.md"_s);
        m.setFilter(u"c+"_s);
        QCOMPARE(titles(m), QStringList{u"#C++"_s});
        QCOMPARE(m.get(0).value(u"subtitle"_s).toString(), u"Page"_s);
        m.setFilter(u"praca"_s); // typed out: nothing to complete
        QVERIFY(titles(m).isEmpty());
        m.setFilter(QString()); // tags first, then pages
        QCOMPARE(titles(m).mid(0, 2), QStringList({u"#praca"_s, u"#żółw"_s}));
        QVERIFY(titles(m).contains(u"#Notatki"_s));
    }

    void slash()
    {
        SuggestionModel m;
        QSignalSpy count(&m, &SuggestionModel::countChanged);
        m.setTrigger({{u"mode"_s, u"slash"_s}, {u"filter"_s, u"przyp"_s}, {u"token"_s, u"/"_s}});
        QCOMPARE(m.get(0).value(u"command"_s).toString(), u"reminder"_s);
        QCOMPARE(m.get(0).value(u"title"_s).toString(), u"Przypomnienie…"_s);
        // The subtitle shows what the command writes, not its name in the other language.
        QCOMPARE(m.get(0).value(u"subtitle"_s).toString(), u"⏰ data godzina"_s);
        QCOMPARE(m.get(0).value(u"hint"_s).toString(), u"⏰"_s);
        QCOMPARE(m.get(0).value(u"icon"_s).toString(), u"bell"_s);
        m.setLocale(u"en"_s);
        QCOMPARE(m.get(0).value(u"title"_s).toString(), u"Reminder…"_s);
        QCOMPARE(m.get(0).value(u"subtitle"_s).toString(), u"⏰ date time"_s);
        m.setLocale(u"pl"_s);
        m.setFilter(u"zadanie"_s);
        QCOMPARE(m.get(0).value(u"command"_s).toString(), u"task"_s);
        QCOMPARE(m.get(0).value(u"subtitle"_s).toString(), u"- [ ] zadanie"_s);
        m.setFilter(u"obliczenia"_s);
        QCOMPARE(m.get(0).value(u"subtitle"_s).toString(), u"blok ```calc"_s);
        // Every command has a syntax line in both languages, never the other language's name.
        for (const SlashCommand &c : slashCommands()) {
            QVERIFY2(!c.syntaxPl.isEmpty() && !c.syntaxEn.isEmpty(), qPrintable(c.id));
            QVERIFY2(c.syntaxPl != c.nameEn && c.syntaxEn != c.namePl, qPrintable(c.id));
        }
        m.setFilter(u"naglowek"_s);
        QCOMPARE(values(m, u"command"_s).mid(0, 3), QStringList({u"h1"_s, u"h2"_s, u"h3"_s}));
        m.setFilter(u"zażółć"_s);
        QCOMPARE(m.count(), 0);
        // A bare "/" lists every command, past `limit`: its first screen mixes the kinds used most
        // (5 rows on touch — Reminder… the fifth —, 8 on desktop), the other task statuses come
        // last in their own order.
        m.setFilter(QString());
        QVERIFY(int(slashCommands().size()) > m.limit());
        QCOMPARE(m.count(), int(slashCommands().size()));
        const QStringList commands = values(m, u"command"_s);
        QCOMPARE(commands.mid(0, 8), QStringList({u"task"_s, u"h1"_s, u"h2"_s, u"table"_s, u"reminder"_s, u"due"_s,
                                                  u"fileLink"_s, u"image"_s}));
        QCOMPARE(commands.mid(commands.size() - 5),
                 QStringList({u"taskDoing"_s, u"taskDone"_s, u"taskWaiting"_s, u"taskCancelled"_s, u"taskNone"_s}));
        // A filter matching more than `limit` commands lists them all too.
        m.setFilter(u"a"_s);
        QVERIFY(m.count() > m.limit());
        QVERIFY(count.size() >= 2);
    }

    void slashExtensionItems()
    {
        // The extensions' items of the `/` menu (b.app.contribute("slash", …)): after the commands
        // and views, by their labels (diacritics folded) or the extension's title, the title as the
        // subtitle; a command "ext:<extension>/<id>", the Markdown as insertText.
        SuggestionModel m;
        const auto row = [](const QString &extension, const QString &title, const QString &id, const QString &label,
                            const QString &icon, const QString &insert) {
            return QVariantMap{{u"extension"_s, extension},
                               {u"title"_s, title},
                               {u"id"_s, id},
                               {u"label"_s, label},
                               {u"icon"_s, icon},
                               {u"insert"_s, insert},
                               {u"command"_s, insert.isEmpty() ? u"x"_s : QString()},
                               {u"key"_s, QString(extension + u'/' + id)}};
        };
        m.setExtensionItems(
                {row(u"silownia"_s, u"Siłownia"_s, u"seria"_s, u"Dodaj serię"_s, u"dumbbell"_s, QString()),
                 row(u"notatki"_s, u"Notatki"_s, u"podpis"_s, u"Podpis z datą"_s, QString(), u"— Zażółć, 2026"_s),
                 row(u"notatki"_s, u"Notatki"_s, u"plan"_s, u"Plan dnia"_s, u"list"_s, u"- Rano\n- Wieczór"_s)});
        QSignalSpy count(&m, &SuggestionModel::countChanged);
        m.setTrigger({{u"mode"_s, u"slash"_s}, {u"filter"_s, u"serie"_s}, {u"token"_s, u"/"_s}});
        // "serię" folds to "serie": the item, the extension's title under it, its icon.
        QCOMPARE(titles(m), QStringList({u"Dodaj serię"_s}));
        QCOMPARE(m.get(0).value(u"subtitle"_s).toString(), u"Siłownia"_s);
        QCOMPARE(m.get(0).value(u"icon"_s).toString(), u"dumbbell"_s);
        QCOMPARE(m.get(0).value(u"kind"_s).toString(), u"command"_s);
        QCOMPARE(m.get(0).value(u"command"_s).toString(), u"ext:silownia/seria"_s);
        QCOMPARE(m.get(0).value(u"extension"_s).toString(), u"silownia"_s);
        QCOMPARE(m.get(0).value(u"itemId"_s).toString(), u"seria"_s);
        QCOMPARE(m.get(0).value(u"insertText"_s).toString(), QString());
        // By the extension's title too, after the labels; no icon: the puzzle.
        m.setFilter(u"notat"_s);
        const int podpis = int(titles(m).indexOf(u"Podpis z datą"_s));
        QVERIFY(podpis >= 0);
        QCOMPARE(m.get(podpis + 1).value(u"title"_s).toString(), u"Plan dnia"_s);
        QCOMPARE(m.get(podpis).value(u"icon"_s).toString(), u"puzzle"_s);
        QCOMPARE(m.get(podpis).value(u"insertText"_s).toString(), u"— Zażółć, 2026"_s);
        // Ranked among the app's commands: a label that starts with what was typed comes before
        // every command that matches less well ("/dod": "Dodaj serię" before commands with "dod"
        // inside a word or in a keyword), after the commands that match as well.
        m.setFilter(u"dod"_s);
        const int dodaj = int(titles(m).indexOf(u"Dodaj serię"_s));
        QVERIFY(dodaj >= 0);
        for (int i = 0; i < m.count(); ++i) {
            const SlashCommand *c = slashCommand(m.get(i).value(u"command"_s).toString());
            if (!c)
                continue;
            const int rank = slashCommandRank(*c, u"dod"_s);
            QVERIFY2(rank == 0 ? i < dodaj : i > dodaj, qPrintable(c->id));
        }
        // A label's start before a word's start before anywhere: "da" starts the word "datą" of
        // the second item, and is only inside "Dodaj" of the first.
        m.setFilter(u"da"_s);
        const QStringList all = titles(m);
        QVERIFY(all.indexOf(u"Podpis z datą"_s) >= 0);
        QVERIFY(all.indexOf(u"Podpis z datą"_s) < all.indexOf(u"Dodaj serię"_s));
        QVERIFY(!all.contains(u"Plan dnia"_s));
        m.setFilter(u"plan"_s); // "Plan dnia" at its start, "Zaplanuj…" inside a word
        QVERIFY(titles(m).indexOf(u"Plan dnia"_s) >= 0);
        QVERIFY(titles(m).indexOf(u"Plan dnia"_s) < titles(m).indexOf(u"Zaplanuj…"_s));
        // A bare "/" lists them after every command.
        m.setFilter(QString());
        QCOMPARE(m.count(), int(slashCommands().size()) + 3);
        QCOMPARE(values(m, u"command"_s).mid(m.count() - 3),
                 QStringList({u"ext:silownia/seria"_s, u"ext:notatki/podpis"_s, u"ext:notatki/plan"_s}));
        // They follow the extensions live (an item removed), and only the slash list shows them.
        m.setExtensionItems({row(u"silownia"_s, u"Siłownia"_s, u"seria"_s, u"Dodaj serię"_s, QString(), QString())});
        QCOMPARE(m.count(), int(slashCommands().size()) + 1);
        m.setFilter(u"zażółć"_s);
        QCOMPARE(m.count(), 0);
        QVERIFY(count.size() >= 3);
        SuggestionModel pages;
        pages.setHost(&m_host);
        pages.setExtensionItems(
                {row(u"silownia"_s, u"Siłownia"_s, u"seria"_s, u"Dodaj serię"_s, QString(), QString())});
        pages.setTrigger({{u"mode"_s, u"pages"_s}, {u"filter"_s, u"Dodaj"_s}});
        QVERIFY(!values(pages, u"command"_s).contains(u"ext:silownia/seria"_s));
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

    void listViewWhileTyping_data()
    {
        QTest::addColumn<bool>("buffered");
        QTest::newRow("default cache buffer") << true;
        QTest::newRow("no cache buffer (the popup)") << false;
    }

    void listViewWhileTyping()
    {
        // A ListView builds the rows below its viewport (its cache buffer) asynchronously, one at
        // a time. A keystroke that reset the model while one of them was still being built made
        // Qt cancel it by its old row ("DelegateModel::cancel: index out range 6 5" on the
        // Android emulator, typing "/przypom"), and a removal of that row instead leaves the view
        // waiting for it. SuggestionPopup's list has no cache buffer: nothing is ever half-made
        // when the items change. (Its cacheBuffer is checked by tst_editor_qml.)
        QFETCH(bool, buffered);
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        QQmlEngine engine;
        HeldIncubation incubation;
        engine.setIncubationController(&incubation);
        // Three rows in view, the rest below them.
        const QByteArray qml = "import QtQuick\n"
                               "ListView {\n"
                               "    width: 200; height: 60\n"
                               "    delegate: Text { required property string title\n"
                               "                     width: 200; height: 20; text: title }\n"
                               "}\n";
        QQmlComponent component(&engine);
        component.setData(qml, QUrl());
        if (component.isError())
            QSKIP(qPrintable(u"Qt Quick is not on the QML import path (QML_IMPORT_PATH): "_s
                             + component.errorString()));
        SuggestionModel m;
        m.setTrigger({{u"mode"_s, u"slash"_s}, {u"filter"_s, QString()}, {u"token"_s, u"/"_s}});
        QVariantMap properties{{u"model"_s, QVariant::fromValue(&m)}};
        if (!buffered)
            properties.insert(u"cacheBuffer"_s, 0);
        QQuickWindow window;
        window.resize(200, 60);
        std::unique_ptr<QQuickItem> view(qobject_cast<QQuickItem *>(component.createWithInitialProperties(properties)));
        QVERIFY(view);
        view->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QCOMPARE(rowText(view.get(), 0), titles(m).at(0));
        if (buffered) {
            // What the popup avoids: a buffer row being built (the harness sees it).
            QTRY_VERIFY(incubation.incubatingObjectCount() > 0);
            incubation.finish();
            QCOMPARE(incubation.incubatingObjectCount(), 0);
            return;
        }
        QTest::failOnWarning(QRegularExpression(u"DelegateModel"_s));
        QTest::qWait(100);
        QCOMPARE(incubation.incubatingObjectCount(), 0);
        const QStringList typed = {u"p"_s, u"pr"_s, u"prz"_s, u"przy"_s, u"przyp"_s, u"przypom"_s, QString(), u"zad"_s};
        for (const QString &filter : typed) {
            m.setFilter(filter);
            QTest::qWait(30);
            QCOMPARE(incubation.incubatingObjectCount(), 0);
            QCOMPARE(view->property("count").toInt(), m.count());
            for (int i = 0; i < std::min(3, m.count()); ++i)
                QCOMPARE(rowText(view.get(), i), titles(m).at(i));
        }
        QCOMPARE(titles(m).at(0), u"Zadanie: do zrobienia"_s);
        // Scrolling builds the rows as they come into view, at once.
        m.setFilter(QString());
        QMetaObject::invokeMethod(view.get(), "positionViewAtEnd");
        QTest::qWait(30);
        QCOMPARE(incubation.incubatingObjectCount(), 0);
        QCOMPARE(rowText(view.get(), m.count() - 1), titles(m).last());
    }

    void refreshesOnIndexChange()
    {
        FakeEditorHost host;
        host.setFile(u"A.md"_s, u"- x #alfa\n"_s);
        SuggestionModel m;
        m.setHost(&host);
        m.setMode(u"tags"_s);
        QCOMPARE(titles(m), QStringList({u"#alfa"_s, u"#A"_s})); // tags, then pages
        host.setFile(u"B.md"_s, u"- y #beta\n"_s);
        Q_EMIT host.indexChanged();
        QCOMPARE(titles(m).size(), 4);
    }

    // The popup closed (clear(), BlockEditor.closeSuggestions()): no mode is left behind, so an
    // index change — every save — ranks no page names and sorts no blocks.
    void closedPopupDoesNoIndexWork()
    {
        CountingHost host;
        host.setFile(u"Projekty.md"_s, u"- plan remontu ^plan1\n"_s);
        host.setFile(u"Zakupy.md"_s, u"- [ ] Mleko, jaja ^mleko\n"_s);
        SuggestionModel m;
        m.setHost(&host);
        for (const QString &mode : {u"pages"_s, u"blocks"_s}) {
            m.setTrigger({{u"mode"_s, mode}, {u"filter"_s, u"pl"_s}});
            QVERIFY(m.count() > 0);
            // Open: it follows the index.
            int asks = host.pageAsks + host.blockAsks;
            Q_EMIT host.indexChanged();
            QVERIFY(host.pageAsks + host.blockAsks > asks);
            QSignalSpy modeChanged(&m, &SuggestionModel::modeChanged);
            m.clear();
            QCOMPARE(modeChanged.size(), 1);
            QCOMPARE(m.mode(), QString());
            QCOMPARE(m.filter(), QString());
            QCOMPARE(m.count(), 0);
            asks = host.pageAsks + host.blockAsks;
            host.setFile(u"Łódź.md"_s, u"- Piotrkowska\n"_s);
            Q_EMIT host.indexChanged();
            Q_EMIT host.indexChanged();
            QCOMPARE(host.pageAsks + host.blockAsks, asks);
            QCOMPARE(m.count(), 0);
            // Cleared again: nothing changes, nothing is told.
            m.clear();
            QCOMPARE(modeChanged.size(), 1);
        }
    }
};

QTEST_MAIN(TestSuggestions)
#include "tst_editor_suggestions.moc"
