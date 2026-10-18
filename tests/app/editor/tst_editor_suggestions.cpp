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
        QCOMPARE(titles(m).at(0), u"Zadanie"_s);
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
        QCOMPARE(titles(m), QStringList({u"#alfa"_s}));
        host.setFile(u"B.md"_s, u"- y #beta\n"_s);
        Q_EMIT host.indexChanged();
        QCOMPARE(titles(m).size(), 2);
    }
};

QTEST_MAIN(TestSuggestions)
#include "tst_editor_suggestions.moc"
