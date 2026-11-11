// Pure edits of one block line for b.replaceBlock (src/core/extensions/blockedit.h).

#include "extensions/blockedit.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::extensions;

class TestExtensionsBlockEdit : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void lineText_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("task"); // "∅" = no task
        QTest::newRow("plain") << u"- [[Przysiad]] 100 kg"_s << u"[[Przysiad]] 100 kg"_s << u"∅"_s;
        QTest::newRow("indented task") << u"\t  - [x] Łąka ✅ 2026-11-04"_s << u"Łąka ✅ 2026-11-04"_s << u"x"_s;
        QTest::newRow("open") << u"  * [ ] a"_s << u"a"_s << u" "_s;
        QTest::newRow("numbered") << u"12. [/] w toku"_s << u"w toku"_s << u"/"_s;
        QTest::newRow("logseq done") << u"- DONE czytanie"_s << u"czytanie"_s << u"x"_s;
        QTest::newRow("logseq todo") << u"- TODO czytanie"_s << u"czytanie"_s << u" "_s;
        QTest::newRow("not a keyword") << u"- DONEX a"_s << u"DONEX a"_s << u"∅"_s;
        QTest::newRow("paragraph") << u"  Akapit  "_s << u"Akapit"_s << u"∅"_s;
        QTest::newRow("empty item") << u"- "_s << QString() << u"∅"_s;
        QTest::newRow("bom") << u"﻿- [x] a"_s << u"a"_s << u"x"_s;
    }
    void lineText()
    {
        QFETCH(QString, line);
        QFETCH(QString, text);
        QFETCH(QString, task);
        QCOMPARE(blockLineText(line), text);
        QCOMPARE(taskCharacter(line).value_or(u"∅"_s), task);
    }

    void replace_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("line");
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("task"); // "" = keep
        QTest::addColumn<QString>("expected"); // "∅" = refused
        QTest::newRow("text") << u"a\r\n- [ ] stare\r\nb"_s << 1 << u"nowe"_s << QString() << u"a\r\n- [ ] nowe\r\nb"_s;
        QTest::newRow("done") << u"- [ ] x\n"_s << 0 << u"y"_s << u"x"_s << u"- [x] y ✅ 2026-11-11\n"_s;
        QTest::newRow("undone") << u"- [x] y ✅ 2026-11-04\n"_s << 0 << u"y"_s << u" "_s << u"- [ ] y\n"_s;
        QTest::newRow("done date kept") << u"  - [x] y ✅ 2026-11-04\n"_s << 0 << u"z"_s << QString() << u"  - [x] z ✅ 2026-11-04\n"_s;
        QTest::newRow("same status") << u"- [x] y ✅ 2026-11-04\n"_s << 0 << u"z"_s << u"X"_s << u"- [x] z ✅ 2026-11-04\n"_s;
        QTest::newRow("block id kept") << u"- a ^abc123\n"_s << 0 << u"b"_s << QString() << u"- b ^abc123\n"_s;
        QTest::newRow("own fields win") << u"- a ✅ 2026-11-04 ^abc\n"_s << 0 << u"b ✅ 2026-11-05"_s << QString() << u"- b ✅ 2026-11-05\n"_s;
        QTest::newRow("plain becomes a task") << u"- a\n"_s << 0 << u"a"_s << u" "_s << u"- [ ] a\n"_s;
        QTest::newRow("logseq keyword") << u"- TODO a\n"_s << 0 << u"b"_s << u"x"_s << u"- DONE b ✅ 2026-11-11\n"_s;
        QTest::newRow("bom and last line") << u"﻿- a"_s << 0 << u"ż"_s << QString() << u"﻿- ż"_s;
        QTest::newRow("paragraph keeps its indentation") << u"  akapit\n"_s << 0 << u"inny"_s << QString() << u"  inny\n"_s;
        QTest::newRow("no such line") << u"- a\n"_s << 5 << u"b"_s << QString() << u"∅"_s;
        QTest::newRow("line break") << u"- a\n"_s << 0 << u"b\nc"_s << QString() << u"∅"_s;
    }
    void replace()
    {
        QFETCH(QString, content);
        QFETCH(int, line);
        QFETCH(QString, text);
        QFETCH(QString, task);
        QFETCH(QString, expected);
        const auto result = replaceBlockText(content, line, text, task.isEmpty() ? std::nullopt : std::optional<QString>(task),
                                             QDate(2026, 11, 11));
        QCOMPARE(result.value_or(u"∅"_s), expected);
    }
};

QTEST_GUILESS_MAIN(TestExtensionsBlockEdit)
#include "tst_extensions_blockedit.moc"
