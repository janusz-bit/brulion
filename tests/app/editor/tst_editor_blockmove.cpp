// Moving blocks to another place of their page (src/app/editor/blockmove.h; docs/ATRAMENT.md M2
// step 4 "Drag to move"): a place between two rows at a depth — deeper (a child of the block above,
// its first or its last), shallower (out of a parent, between its ancestors' siblings), into
// another parent, among siblings —, the blocks' subtrees along, the plan's steps applied to the
// document as content in → content out: every byte the move does not touch kept (CRLF line
// endings, other blocks' text, the blank lines), numbered lists renumbered. Refused: inside the
// blocks themselves, under one of them, under a block that is no list item, too deep for the block
// above, a place taking the next block's parent away, above the page's properties, a paragraph
// below the top level, blocks that are no consecutive siblings, the place where they are.
#include "blockmove.h"

#include <QTest>

using namespace brulion;
using namespace brulion::editor;
using namespace Qt::StringLiterals;
using markdown::BlockDocument;

Q_DECLARE_METATYPE(brulion::editor::BlockPlace)

namespace {

// The document with rows [first, last] moved to `place` by the plan's steps; "" when refused.
QString moved(const QString &content, int first, int last, BlockPlace place)
{
    BlockDocument doc(content);
    const std::optional<QList<MoveStep>> steps = planBlockMove(doc, first, last, place);
    if (!steps)
        return QString();
    const QList<int> tops = doc.rangeTops(first, last);
    const quint64 a = doc.at(tops.first()).uid;
    const quint64 b = doc.at(tops.last()).uid;
    for (MoveStep s : *steps) {
        if (!applyMoveStep(doc, a, b, s).changed())
            return u"(a step changed nothing)"_s;
    }
    return doc.toString();
}

const QString Fruit = u"- owoce\n\t- jabłka\n\t- gruszki\n- warzywa\n\t- marchew\n\t- seler\n- zioła\n"_s;

} // namespace

class TestEditorBlockMove : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void move_data();
    void move();
    void refused_data();
    void refused();
    void placeOfBlocksFromElsewhere();
    void placeOfBlocksMovedHere();
    void stepsOnTheDocumentItself();
};

void TestEditorBlockMove::move_data()
{
    QTest::addColumn<QString>("content");
    QTest::addColumn<int>("first");
    QTest::addColumn<int>("last");
    QTest::addColumn<BlockPlace>("place");
    QTest::addColumn<QString>("expected");

    QTest::newRow("deeper: a child of the block above") << u"- jabłka\n- gruszki\n- śliwki\n"_s << 1 << 1 << BlockPlace{1, 1}
                                                       << u"- jabłka\n\t- gruszki\n- śliwki\n"_s;
    QTest::newRow("deeper, past its next sibling") << u"- jabłka\n- gruszki\n- śliwki\n"_s << 0 << 0 << BlockPlace{3, 1}
                                                  << u"- gruszki\n- śliwki\n\t- jabłka\n"_s;
    QTest::newRow("out of its parent, after it") << Fruit << 1 << 1 << BlockPlace{3, 0}
                                                 << u"- owoce\n\t- gruszki\n- jabłka\n- warzywa\n\t- marchew\n\t- seler\n- zioła\n"_s;
    QTest::newRow("out of its parent, to the page's end") << Fruit << 2 << 2 << BlockPlace{7, 0}
                                                          << u"- owoce\n\t- jabłka\n- warzywa\n\t- marchew\n\t- seler\n- zioła\n- gruszki\n"_s;
    QTest::newRow("out of its parent, to the page's start") << Fruit << 5 << 5 << BlockPlace{0, 0}
                                                            << u"- seler\n- owoce\n\t- jabłka\n\t- gruszki\n- warzywa\n\t- marchew\n- zioła\n"_s;
    QTest::newRow("into another parent, its last child") << Fruit << 1 << 1 << BlockPlace{6, 1}
                                                         << u"- owoce\n\t- gruszki\n- warzywa\n\t- marchew\n\t- seler\n\t- jabłka\n- zioła\n"_s;
    QTest::newRow("into another parent, its first child") << Fruit << 1 << 1 << BlockPlace{4, 1}
                                                          << u"- owoce\n\t- gruszki\n- warzywa\n\t- jabłka\n\t- marchew\n\t- seler\n- zioła\n"_s;
    QTest::newRow("into another parent upwards") << Fruit << 5 << 5 << BlockPlace{2, 1}
                                                 << u"- owoce\n\t- jabłka\n\t- seler\n\t- gruszki\n- warzywa\n\t- marchew\n- zioła\n"_s;
    QTest::newRow("a level deeper inside its parent") << Fruit << 2 << 2 << BlockPlace{2, 2}
                                                      << u"- owoce\n\t- jabłka\n\t\t- gruszki\n- warzywa\n\t- marchew\n\t- seler\n- zioła\n"_s;
    QTest::newRow("among its siblings") << Fruit << 6 << 6 << BlockPlace{0, 0}
                                        << u"- zioła\n- owoce\n\t- jabłka\n\t- gruszki\n- warzywa\n\t- marchew\n\t- seler\n"_s;
    QTest::newRow("with its children along") << Fruit << 0 << 0 << BlockPlace{6, 1}
                                             << u"- warzywa\n\t- marchew\n\t- seler\n\t- owoce\n\t\t- jabłka\n\t\t- gruszki\n- zioła\n"_s;
    QTest::newRow("two siblings at once") << Fruit << 4 << 5 << BlockPlace{3, 1}
                                          << u"- owoce\n\t- jabłka\n\t- gruszki\n\t- marchew\n\t- seler\n- warzywa\n- zioła\n"_s;
    QTest::newRow("two levels up") << u"- dom\n\t- kuchnia\n\t\t- zlew\n- ogród\n"_s << 2 << 2 << BlockPlace{3, 0}
                                   << u"- dom\n\t- kuchnia\n- zlew\n- ogród\n"_s;
    QTest::newRow("up and down again") << u"- dom\n\t- kuchnia\n\t\t- zlew\n- ogród\n\t- grządka\n"_s << 2 << 2
                                       << BlockPlace{5, 2} << u"- dom\n\t- kuchnia\n- ogród\n\t- grządka\n\t\t- zlew\n"_s;
    QTest::newRow("under a folded-looking parent's last child") << u"- a\n\t- a1\n\t\t- a11\n- b\n"_s << 3 << 3
                                                               << BlockPlace{3, 2} << u"- a\n\t- a1\n\t\t- a11\n\t\t- b\n"_s;
    QTest::newRow("CRLF and the other blocks kept") << u"- pierwszy  \r\n- drugi ^id1\r\n  ciąg dalszy\r\n- trzeci\r\n"_s << 2 << 2
                                                    << BlockPlace{1, 1}
                                                    << u"- pierwszy  \r\n\t- trzeci\r\n- drugi ^id1\r\n  ciąg dalszy\r\n"_s;
    QTest::newRow("a numbered list renumbered") << u"1. pierwszy\n2. drugi\n3. trzeci\n"_s << 2 << 2 << BlockPlace{0, 0}
                                                << u"1. trzeci\n2. pierwszy\n3. drugi\n"_s;
    QTest::newRow("a paragraph among the top level") << u"Akapit źdźbło.\n\n- lista\n"_s << 0 << 0 << BlockPlace{2, 0}
                                                     << u"- lista\n\nAkapit źdźbło.\n"_s;
    QTest::newRow("after the page's properties") << u"---\ntytuł: Zakupy\n---\n- mleko\n- chleb\n"_s << 2 << 2
                                                 << BlockPlace{1, 0} << u"---\ntytuł: Zakupy\n---\n- chleb\n- mleko\n"_s;
}

void TestEditorBlockMove::move()
{
    QFETCH(QString, content);
    QFETCH(int, first);
    QFETCH(int, last);
    QFETCH(BlockPlace, place);
    QFETCH(QString, expected);
    QCOMPARE(moved(content, first, last, place), expected);
}

void TestEditorBlockMove::refused_data()
{
    QTest::addColumn<QString>("content");
    QTest::addColumn<int>("first");
    QTest::addColumn<int>("last");
    QTest::addColumn<BlockPlace>("place");

    QTest::newRow("where it is") << Fruit << 1 << 1 << BlockPlace{1, 1};
    QTest::newRow("where it is, after itself") << Fruit << 1 << 1 << BlockPlace{2, 1};
    QTest::newRow("inside its own subtree") << Fruit << 0 << 0 << BlockPlace{2, 1};
    QTest::newRow("under itself") << Fruit << 6 << 6 << BlockPlace{7, 1};
    QTest::newRow("under its own child") << Fruit << 0 << 0 << BlockPlace{3, 2};
    QTest::newRow("too deep for the block above") << Fruit << 6 << 6 << BlockPlace{3, 3};
    QTest::newRow("deeper than the start allows") << u"- jabłka\n- gruszki\n"_s << 1 << 1 << BlockPlace{0, 1};
    QTest::newRow("taking the next block's parent away") << Fruit << 6 << 6 << BlockPlace{2, 0};
    QTest::newRow("under a paragraph") << u"Akapit.\n\n- lista\n- druga\n"_s << 2 << 2 << BlockPlace{1, 1};
    QTest::newRow("a paragraph below the top level") << u"- lista\n\nAkapit.\n"_s << 1 << 1 << BlockPlace{1, 1};
    QTest::newRow("above the page's properties") << u"---\ntytuł: Zakupy\n---\n- mleko\n- chleb\n"_s << 2 << 2
                                                 << BlockPlace{0, 0};
    QTest::newRow("rising through levels") << Fruit << 2 << 3 << BlockPlace{7, 0};
    QTest::newRow("a gap past the end") << Fruit << 1 << 1 << BlockPlace{8, 0};
    QTest::newRow("a negative depth") << Fruit << 1 << 1 << BlockPlace{3, -1};
    QTest::newRow("no blocks") << Fruit << 3 << 2 << BlockPlace{0, 0};
}

void TestEditorBlockMove::refused()
{
    QFETCH(QString, content);
    QFETCH(int, first);
    QFETCH(int, last);
    QFETCH(BlockPlace, place);
    QCOMPARE(moved(content, first, last, place), QString());
}

void TestEditorBlockMove::placeOfBlocksFromElsewhere()
{
    // Blocks coming from another page (none of them here): the parent and the sibling they go before.
    const BlockDocument doc(Fruit);
    std::optional<PlaceTarget> t = placeTarget(doc, {4, 1});
    QVERIFY(t);
    QCOMPARE(t->parent, doc.at(3).uid);
    QCOMPARE(t->before, doc.at(4).uid);
    t = placeTarget(doc, {6, 1});
    QVERIFY(t);
    QCOMPARE(t->parent, doc.at(3).uid);
    QCOMPARE(t->before, quint64(0)); // the end of its children
    t = placeTarget(doc, {7, 0});
    QVERIFY(t);
    QCOMPARE(t->parent, quint64(0));
    QCOMPARE(t->before, quint64(0));
    t = placeTarget(doc, {3, 0});
    QVERIFY(t);
    QCOMPARE(t->before, doc.at(3).uid);
    // Under nothing, too deep, before a deeper block.
    QVERIFY(!placeTarget(doc, {0, 1}));
    QVERIFY(!placeTarget(doc, {3, 3}));
    QVERIFY(!placeTarget(doc, {2, 0}));
}

void TestEditorBlockMove::placeOfBlocksMovedHere()
{
    // The blocks moved are rows [first, end): a place is never under one of them or inside them.
    const BlockDocument doc(Fruit);
    QVERIFY(!placeTarget(doc, {7, 1}, 6, 7)); // under "zioła", the block moved
    QVERIFY(!placeTarget(doc, {3, 1}, 0, 3)); // under "owoce", the first row moved
    QVERIFY(!placeTarget(doc, {2, 1}, 0, 3)); // inside its subtree
    QVERIFY(!placeTarget(doc, {1, 2}, 0, 3));
    // Right before or after them, the next block jumped over: the sibling after them.
    std::optional<PlaceTarget> t = placeTarget(doc, {0, 0}, 0, 3);
    QVERIFY(t);
    QCOMPARE(t->before, doc.at(3).uid);
    t = placeTarget(doc, {3, 0}, 0, 3);
    QVERIFY(t);
    QCOMPARE(t->before, doc.at(3).uid);
    // Without them, under "owoce" there.
    t = placeTarget(doc, {3, 1});
    QVERIFY(t);
    QCOMPARE(t->parent, doc.at(0).uid);
}

void TestEditorBlockMove::stepsOnTheDocumentItself()
{
    // The plan leaves the document it was given as it is; the steps follow the blocks by their uids.
    BlockDocument doc(Fruit);
    const quint64 apples = doc.at(1).uid;
    const std::optional<QList<MoveStep>> steps = planBlockMove(doc, 1, 1, {6, 1});
    QVERIFY(steps && !steps->isEmpty());
    QCOMPARE(doc.toString(), Fruit);
    for (MoveStep s : *steps)
        QVERIFY(applyMoveStep(doc, apples, apples, s).changed());
    QCOMPARE(doc.indexOfUid(apples), 5);
    QCOMPARE(doc.at(5).depth, 1);
    // Unknown blocks: nothing.
    QVERIFY(!applyMoveStep(doc, 0, apples, MoveStep::Up).changed());
}

QTEST_GUILESS_MAIN(TestEditorBlockMove)
#include "tst_editor_blockmove.moc"
