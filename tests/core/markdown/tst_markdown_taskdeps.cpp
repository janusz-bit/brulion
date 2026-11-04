// Obsidian Tasks dependencies (markdown/taskdeps.h): 🆔 / ⛔ parsed into Task, written in Obsidian
// Tasks' order, ids with odd characters, and the subtask progress of a parent task.
#include "markdown/parse.h"
#include "markdown/taskdeps.h"

#include <QSet>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::markdown;

class TstMarkdownTaskDeps : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void parse_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("id");
        QTest::addColumn<QStringList>("dependsOn");
        QTest::addColumn<QString>("text");

        QTest::newRow("obsidian order") << u"- [ ] Zadzwonić do Łukasza 🆔 abc123 ⛔ def456 ⏫ 📅 2026-11-05"_s
                                        << u"abc123"_s << QStringList{u"def456"_s} << u"Zadzwonić do Łukasza"_s;
        QTest::newRow("comma list with blanks")
                << u"- [ ] a ⛔ x1, y_2 ,z-3"_s << QString() << QStringList{u"x1"_s, u"y_2"_s, u"z-3"_s} << u"a"_s;
        QTest::newRow("two tokens, duplicates once")
                << u"- [ ] a ⛔ x ⛔ y,x"_s << QString() << QStringList{u"x"_s, u"y"_s} << u"a"_s;
        QTest::newRow("first id wins") << u"- [ ] a 🆔 one 🆔 two"_s << u"one"_s << QStringList() << u"a"_s;
        QTest::newRow("variation selector") << u"- [ ] a ⛔️ x"_s << QString() << QStringList{u"x"_s} << u"a"_s;
        QTest::newRow("odd characters end the id")
                << u"- [ ] a 🆔 zadanie-ąę"_s << u"zadanie-"_s << QStringList() << u"aąę"_s;
        QTest::newRow("no id after the emoji") << u"- [ ] a ⛔ ,x"_s << QString() << QStringList() << u"a ⛔ ,x"_s;
        QTest::newRow("in code") << u"- [ ] a `🆔 abc`"_s << QString() << QStringList() << u"a `🆔 abc`"_s;
        QTest::newRow("logseq") << u"- TODO zadanie 🆔 L1"_s << u"L1"_s << QStringList() << u"zadanie"_s;
        QTest::newRow("case kept") << u"- [ ] a 🆔 AbC ⛔ aBc"_s << u"AbC"_s << QStringList{u"aBc"_s} << u"a"_s;
    }
    void parse()
    {
        QFETCH(QString, line);
        QFETCH(QString, id);
        QFETCH(QStringList, dependsOn);
        QFETCH(QString, text);
        const ParsedNote note = parseNote(u"a.md"_s, line);
        QCOMPARE(note.tasks.size(), 1);
        QCOMPARE(note.tasks[0].id, id);
        QCOMPARE(note.tasks[0].dependsOn, dependsOn);
        QCOMPARE(note.tasks[0].text, text);
    }

    void validIds_data()
    {
        QTest::addColumn<QString>("id");
        QTest::addColumn<bool>("valid");
        QTest::newRow("plain") << u"abc123"_s << true;
        QTest::newRow("dash and underscore") << u"a-b_C"_s << true;
        QTest::newRow("empty") << QString() << false;
        QTest::newRow("space") << u"a b"_s << false;
        QTest::newRow("comma") << u"a,b"_s << false;
        QTest::newRow("polish") << u"zażółć"_s << false;
        QTest::newRow("emoji") << u"⛔"_s << false;
    }
    void validIds()
    {
        QFETCH(QString, id);
        QFETCH(bool, valid);
        QCOMPARE(isValidTaskId(id), valid);
    }

    void generate()
    {
        QSet<QString> seen;
        for (int k = 0; k < 50; ++k) {
            const QString id = generateTaskId([&](const QString &c) { return seen.contains(c); });
            QCOMPARE(id.size(), 6);
            QVERIFY(isValidTaskId(id));
            QCOMPARE(id, id.toLower());
            QVERIFY(!seen.contains(id));
            seen.insert(id);
        }
        // Everything of six characters taken: longer ids.
        int calls = 0;
        const QString longer = generateTaskId([&](const QString &c) {
            ++calls;
            return c.size() == 6;
        });
        QCOMPARE(longer.size(), 7);
        QVERIFY(calls > 100);
    }

    void setId_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("line");
        QTest::addColumn<QString>("id");
        QTest::addColumn<QString>("expected");

        QTest::newRow("before the first token") << u"- [ ] Kupić mleko ⏫ 📅 2026-11-05\n"_s << 0 << u"abc123"_s
                                                << u"- [ ] Kupić mleko 🆔 abc123 ⏫ 📅 2026-11-05\n"_s;
        QTest::newRow("at the end, before a block id")
                << u"- [ ] Kupić mleko  ^blok1\n"_s << 0 << u"abc123"_s << u"- [ ] Kupić mleko 🆔 abc123  ^blok1\n"_s;
        QTest::newRow("no tokens") << u"- [ ] żółw\n"_s << 0 << u"x"_s << u"- [ ] żółw 🆔 x\n"_s;
        QTest::newRow("before ⛔") << u"- [ ] a ⛔ q 📅 2026-11-05\n"_s << 0 << u"x"_s
                                   << u"- [ ] a 🆔 x ⛔ q 📅 2026-11-05\n"_s;
        QTest::newRow("replaced") << u"- [ ] a 🆔 old ⏫\n"_s << 0 << u"new1"_s << u"- [ ] a 🆔 new1 ⏫\n"_s;
        QTest::newRow("removed") << u"- [ ] a 🆔 old ⏫\n"_s << 0 << QString() << u"- [ ] a ⏫\n"_s;
        QTest::newRow("logseq priority stays first")
                << u"- TODO [#A] zadanie\n"_s << 0 << u"x"_s << u"- TODO [#A] zadanie 🆔 x\n"_s;
        QTest::newRow("token in code is no token")
                << u"- [ ] a `📅 2026-11-05`\n"_s << 0 << u"x"_s << u"- [ ] a `📅 2026-11-05` 🆔 x\n"_s;
        QTest::newRow("any line of the item, CRLF")
                << u"- [ ] a\r\n  opis\r\n- [ ] b\r\n"_s << 1 << u"x"_s << u"- [ ] a 🆔 x\r\n  opis\r\n- [ ] b\r\n"_s;
        QTest::newRow("invalid id") << u"- [ ] a\n"_s << 0 << u"z ę"_s << u"- [ ] a\n"_s;
        QTest::newRow("no task") << u"- a\n"_s << 0 << u"x"_s << u"- a\n"_s;
        QTest::newRow("same id") << u"- [ ] a 🆔 x\n"_s << 0 << u"x"_s << u"- [ ] a 🆔 x\n"_s;
    }
    void setId()
    {
        QFETCH(QString, content);
        QFETCH(int, line);
        QFETCH(QString, id);
        QFETCH(QString, expected);
        QCOMPARE(setTaskId(content, line, id), expected);
    }

    void dependencies_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("add");
        QTest::addColumn<QString>("added");
        QTest::addColumn<QString>("remove");
        QTest::addColumn<QString>("removed");

        QTest::newRow("after the id") << u"- [ ] a 🆔 me ⏫ 📅 2026-11-05\n"_s << u"dep1"_s
                                      << u"- [ ] a 🆔 me ⛔ dep1 ⏫ 📅 2026-11-05\n"_s << u"dep1"_s
                                      << u"- [ ] a 🆔 me ⏫ 📅 2026-11-05\n"_s;
        QTest::newRow("without an id: before the tokens") << u"- [ ] a 🔁 every day 📅 2026-11-05\n"_s << u"dep1"_s
                                                          << u"- [ ] a ⛔ dep1 🔁 every day 📅 2026-11-05\n"_s
                                                          << u"dep1"_s << u"- [ ] a 🔁 every day 📅 2026-11-05\n"_s;
        QTest::newRow("appended to the list")
                << u"- [ ] a ⛔ x,y\n"_s << u"z"_s << u"- [ ] a ⛔ x,y,z\n"_s << u"y"_s << u"- [ ] a ⛔ x,z\n"_s;
        QTest::newRow("already listed") << u"- [ ] a ⛔ x, y\n"_s << u"y"_s << u"- [ ] a ⛔ x, y\n"_s << u"q"_s
                                        << u"- [ ] a ⛔ x, y\n"_s;
        QTest::newRow("plain end") << u"- [ ] Łąka\n"_s << u"k"_s << u"- [ ] Łąka ⛔ k\n"_s << u"k"_s
                                   << u"- [ ] Łąka\n"_s;
        QTest::newRow("removed from every token")
                << u"- [ ] a ⛔ x ⛔ y,x\n"_s << u"y"_s << u"- [ ] a ⛔ x ⛔ y,x\n"_s << u"x"_s << u"- [ ] a ⛔ y\n"_s;
        QTest::newRow("invalid id") << u"- [ ] a\n"_s << u"a,b"_s << u"- [ ] a\n"_s << u"a,b"_s << u"- [ ] a\n"_s;
    }
    void dependencies()
    {
        QFETCH(QString, content);
        QFETCH(QString, add);
        QFETCH(QString, added);
        QFETCH(QString, remove);
        QFETCH(QString, removed);
        const QString a = addTaskDependency(content, 0, add);
        QCOMPARE(a, added);
        QCOMPARE(removeTaskDependency(a, 0, remove), removed);
        // What was written reads back.
        if (a != content)
            QVERIFY(parseNote(u"a.md"_s, a).tasks[0].dependsOn.contains(add));
    }

    void progress_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("block");
        QTest::addColumn<int>("done");
        QTest::addColumn<int>("total");

        const QString tree = u"- [ ] projekt\n"
                             "  - [x] jeden\n"
                             "  - [ ] dwa\n"
                             "    - [x] dwa a\n"
                             "    - [-] dwa b (anulowane)\n"
                             "  - notatka bez zadania\n"
                             "    - [/] pod notatką\n"
                             "- [ ] inne\n"_s;
        QTest::newRow("nested, cancelled left out") << tree << 0 << 2 << 4;
        QTest::newRow("a subtask with subtasks") << tree << 2 << 1 << 1;
        QTest::newRow("no subtasks") << tree << 7 << 0 << 0;
        QTest::newRow("only cancelled") << u"- [ ] a\n  - [-] b\n"_s << 0 << 0 << 0;
        QTest::newRow("logseq") << u"- TODO a\n\t- DONE b\n\t- LATER c\n"_s << 0 << 1 << 2;
        QTest::newRow("out of range") << u"- [ ] a\n"_s << 5 << 0 << 0;
    }
    void progress()
    {
        QFETCH(QString, content);
        QFETCH(int, block);
        QFETCH(int, done);
        QFETCH(int, total);
        const ParsedNote note = parseNote(u"a.md"_s, content);
        QCOMPARE(subtaskProgress(note, block), (SubtaskProgress{done, total}));
    }
};

QTEST_GUILESS_MAIN(TstMarkdownTaskDeps)
#include "tst_markdown_taskdeps.moc"
