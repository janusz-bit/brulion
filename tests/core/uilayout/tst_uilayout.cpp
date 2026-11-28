// The interface's layout as text (uilayout/uilayout.h, docs/NOTEBOOK-SETTINGS.md "The layout
// language"): calls read into places, groups and hidden items; comments and blank lines; what does
// not read left out with a warning naming its line — unknown places and items, duplicates, a sixth
// bottom button, tabs neither on nor off, malformed calls —; printed back and read again the same;
// the block of a note; the items a place shows.
#include "uilayout/uilayout.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::uilayout;

class TestUiLayout : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void readsPlaces()
    {
        const Layout l = parse(u"// Mój układ\n#rail(journals, tasks, |, calendar)\n\n"
                               u"  #panel( favorites , recent, -pages )\n#bottom(journals, tasks, search)\n"_s);
        QVERIFY2(l.warnings.isEmpty(), qPrintable(l.warnings.join(u" | "_s)));
        QCOMPARE(l.places.size(), 3);
        QVERIFY(l.place(u"rail"));
        QCOMPARE(l.place(u"rail")->items, (QStringList{u"journals"_s, u"tasks"_s, u"|"_s, u"calendar"_s}));
        QCOMPARE(l.place(u"panel")->items, (QStringList{u"favorites"_s, u"recent"_s, u"-pages"_s}));
        QCOMPARE(l.place(u"more"), nullptr);
        QCOMPARE(shown(l, u"rail", {}), (QStringList{u"journals"_s, u"tasks"_s, u"calendar"_s}));
        QCOMPARE(shown(l, u"panel", {}), (QStringList{u"favorites"_s, u"recent"_s}));
        QCOMPARE(shown(l, u"more", {u"a"_s}), QStringList{u"a"_s});
    }

    void warnings_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("rail");
        QTest::addColumn<QString>("warning");
        QTest::newRow("unknown place") << "#sufit(a)\n#rail(tasks)" << "tasks" << "line 1: no place “sufit”";
        QTest::newRow("unknown item") << "#rail(tasks, żółw)" << "tasks" << "line 1: “żółw” is no item of rail";
        QTest::newRow("twice") << "#rail(tasks, tasks)" << "tasks" << "line 1: “tasks” is in rail twice";
        QTest::newRow("hidden and shown") << "#rail(tasks, -tasks)" << "tasks" << "line 1: “tasks” is in rail twice";
        QTest::newRow("second rail") << "#rail(tasks)\n#rail(search)" << "tasks" << "line 2: a second rail";
        QTest::newRow("no call") << "rail(tasks)\n#rail(search)" << "search" << "line 1: no call";
        QTest::newRow("unclosed") << "#rail(tasks, search\n#rail(graph)" << "graph" << "line 1: no call";
        QTest::newRow("empty item") << "#rail(tasks, , search)" << "tasks,search" << "line 1: an empty item";
    }
    void warnings()
    {
        QFETCH(QString, text);
        QFETCH(QString, rail);
        QFETCH(QString, warning);
        const Layout l = parse(text);
        QCOMPARE(l.warnings.size(), 1);
        QCOMPARE(l.warnings.first(), warning);
        QCOMPARE(shown(l, u"rail", {}).join(u','), rail);
    }

    void bottom()
    {
        Layout l = parse(u"#bottom(journals, tasks, search, calendar, graph, tags)\n#tabs(off)"_s);
        QCOMPARE(shown(l, u"bottom", {}).size(), MaxBottom);
        QVERIFY(!shown(l, u"bottom", {}).contains(u"tags"_s));
        QCOMPARE(l.warnings.size(), 2);
        QCOMPARE(l.warnings.at(0), u"line 1: more than 5 bottom buttons: “tags” left out"_s);
        QCOMPARE(l.warnings.at(1), u"line 2: no place “tabs”"_s);
        // (A group in the bottom bar is no button.)
        l = parse(u"#bottom(journals, |, tasks)"_s);
        QCOMPARE(shown(l, u"bottom", {}), (QStringList{u"journals"_s, u"tasks"_s}));
        // (Nor is a hidden one: five buttons beside them all fit.)
        l = parse(u"#bottom(journals, |, tasks, -tags, search, calendar, graph)"_s);
        QVERIFY2(l.warnings.isEmpty(), qPrintable(l.warnings.join(u" | "_s)));
        QCOMPARE(shown(l, u"bottom", {}).size(), MaxBottom);
    }

    void printedAndReadAgain()
    {
        const Layout l = parse(u"#rail(journals,tasks,|,calendar)\n#panel(-pages, recent)"_s);
        const QString text = print(l);
        QCOMPARE(text, u"#rail(journals, tasks, |, calendar)\n#panel(-pages, recent)\n"_s);
        QCOMPARE(parse(text).places, l.places);
        QCOMPARE(print(Layout{}), QString());
    }

    void blockOfANote()
    {
        const QString note = u"---\nbrulion: settings\n---\n# Ustawienia\n\n```brulion-layout\n#rail(tasks)\n#tabs(off)\n```\n\n"
                             u"```brulion-layout\n#rail(search)\n```\n"_s;
        QCOMPARE(blockOf(note), u"#rail(tasks)\n#tabs(off)\n"_s);
        QCOMPARE(blockOf(u"```\n#rail(tasks)\n```\n"_s), QString());
        QCOMPARE(blockOf(u"```brulion-layout\n#rail(tasks)\n"_s), QString()); // (never closed)
        QCOMPARE(blockOf(u"~~~brulion-layout\r\n#rail(tasks)\r\n~~~\r\n"_s), u"#rail(tasks)\n"_s);
    }

    void vocabulary()
    {
        const Vocabulary &v = defaultVocabulary();
        QVERIFY(v.value(u"rail"_s).contains(u"journals"_s));
        QVERIFY(v.value(u"panel"_s).contains(u"favorites"_s));
        QVERIFY(v.value(u"bottom"_s).contains(u"graph"_s));
        QVERIFY(!v.contains(u"tabs"_s));
        // Another vocabulary: its places only.
        const Layout l = parse(u"#a(x, y)\n#rail(tasks)"_s, Vocabulary{{u"a"_s, {u"x"_s, u"y"_s}}});
        QCOMPARE(shown(l, u"a", {}), (QStringList{u"x"_s, u"y"_s}));
        QCOMPARE(l.warnings, QStringList{u"line 2: no place “rail”"_s});
    }
};

QTEST_GUILESS_MAIN(TestUiLayout)
#include "tst_uilayout.moc"
