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
};

QTEST_MAIN(TestSuggestions)
#include "tst_editor_suggestions.moc"
