// GFM tables: parsing, serialisation, locating (ported from the v1 table.test.ts).
#include "calc/table.h"

#include <QTest>

#include <climits>

using namespace brulion::calc;
using namespace Qt::StringLiterals;

namespace {
constexpr TableAlign N = TableAlign::None;
constexpr TableAlign L = TableAlign::Left;
constexpr TableAlign C = TableAlign::Center;
constexpr TableAlign R = TableAlign::Right;

QString slice(const QString &content, const TableSpan &t)
{
    return content.mid(t.from, t.to - t.from);
}

enum class Op { InsertRow, RemoveRow, MoveRow, InsertColumn, RemoveColumn, MoveColumn };

TableModel shop()
{
    TableModel m;
    m.header = {u"Pozycja"_s, u"Cena"_s, u"Ilość"_s};
    m.aligns = {L, R, C};
    m.rows = {{u"Płytki"_s, u"89"_s, u"2"_s}, {u"Żółta farba"_s, u"40"_s, u"1"_s}, {u"Wałek"_s, u"12,50"_s, u"3"_s}};
    return m;
}

QList<QList<int>> lineRanges(const QList<TableSpan> &tables)
{
    QList<QList<int>> out;
    for (const TableSpan &t : tables)
        out.append({t.startLine, t.endLine});
    return out;
}
} // namespace

class TestCalcTable : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void splitRow_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QStringList>("cells");
        QTest::newRow("pipes") << u"| a | b |"_s << QStringList{u"a"_s, u"b"_s};
        QTest::newRow("no outer pipes") << u"a | b"_s << QStringList{u"a"_s, u"b"_s};
        QTest::newRow("no spaces") << u"|a|b"_s << QStringList{u"a"_s, u"b"_s};
        QTest::newRow("escaped pipe") << u"| a \\| b | c |"_s << QStringList{u"a | b"_s, u"c"_s};
        QTest::newRow("empty cell") << u"||  x |"_s << QStringList{QString(), u"x"_s};
        QTest::newRow("polish") << u"| zażółć | gęślą |"_s << QStringList{u"zażółć"_s, u"gęślą"_s};
        QTest::newRow("indented") << u"  | indented |  "_s << QStringList{u"indented"_s};
    }
    void splitRow()
    {
        QFETCH(QString, line);
        QFETCH(QStringList, cells);
        QCOMPARE(splitTableRow(line), cells);
    }

    void delimiterRow_data()
    {
        // Expected alignments as letters: N(one), L(eft), C(enter), R(ight); "-" = not a delimiter row.
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("aligns");
        QTest::newRow("plain") << u"|---|---|"_s << u"NN"_s;
        QTest::newRow("aligned") << u"| :-- | --: | :-: | - |"_s << u"LRCN"_s;
        QTest::newRow("no outer pipes") << u"---|---"_s << u"NN"_s;
        QTest::newRow("single") << u":---"_s << u"L"_s;
        QTest::newRow("one dash") << u"| - |"_s << u"N"_s;
        QTest::newRow("list item") << u"- foo"_s << u"-"_s;
        QTest::newRow("list with pipe") << u"- | -"_s << u"-"_s;
        QTest::newRow("letter") << u"|---|x|"_s << u"-"_s;
        QTest::newRow("empty middle") << u"| --- | | --- |"_s << u"-"_s;
        QTest::newRow("empty") << QString() << u"-"_s;
        QTest::newRow("pipe") << u"|"_s << u"-"_s;
        QTest::newRow("dash") << u"-"_s << u"-"_s;
        QTest::newRow("colons only") << u"|:|"_s << u"-"_s;
    }
    void delimiterRow()
    {
        QFETCH(QString, line);
        QFETCH(QString, aligns);
        const auto parsed = parseDelimiterRow(line);
        QString actual = u"-"_s;
        if (parsed) {
            actual.clear();
            for (const TableAlign a : *parsed)
                actual += a == L ? u'L' : a == C ? u'C' : a == R ? u'R' : u'N';
        }
        QCOMPARE(actual, aligns);
    }

    void parseTable()
    {
        const auto t = brulion::calc::parseTable(
            u"| Produkt | Cena | Ilość |\n|:--|--:|:-:|\n| Jabłka | 3,50 zł | 4 |\n| Gruszki | 5 zł | 2 |");
        QVERIFY(t);
        QCOMPARE(t->header, (QStringList{u"Produkt"_s, u"Cena"_s, u"Ilość"_s}));
        QCOMPARE(t->aligns, (QList<TableAlign>{L, R, C}));
        QCOMPARE(t->rows, (QList<QStringList>{{u"Jabłka"_s, u"3,50 zł"_s, u"4"_s}, {u"Gruszki"_s, u"5 zł"_s, u"2"_s}}));
    }

    void raggedRows()
    {
        const auto t = brulion::calc::parseTable(u"a | b | c\n--|--|--\n1\n1 | 2 | 3 | 4");
        QCOMPARE(t->rows, (QList<QStringList>{{u"1"_s, QString(), QString()}, {u"1"_s, u"2"_s, u"3"_s}}));
    }

    void escapedPipesAndEmptyCells()
    {
        QCOMPARE(brulion::calc::parseTable(u"| x | y |\n|---|---|\n| a \\| b | |")->rows,
                 (QList<QStringList>{{u"a | b"_s, QString()}}));
    }

    void tableWithoutBody()
    {
        const auto t = brulion::calc::parseTable(u"\n\n| a |\r\n| --- |\r\n");
        QVERIFY(t);
        QCOMPARE(t->header, QStringList{u"a"_s});
        QCOMPARE(t->aligns, QList<TableAlign>{N});
        QVERIFY(t->rows.isEmpty());
    }

    void stopsAtBlankLine()
    {
        QCOMPARE(brulion::calc::parseTable(u"| a |\n|---|\n| 1 |\n\n| 2 |")->rows, (QList<QStringList>{{u"1"_s}}));
    }

    void rejects_data()
    {
        QTest::addColumn<QString>("markdown");
        QTest::newRow("no pipe in header") << u"a\n---"_s;
        QTest::newRow("cell count mismatch") << u"| a | b |\n|---|\n| 1 | 2 |"_s;
        QTest::newRow("no delimiter") << u"| a | b |\n| 1 | 2 |"_s;
        QTest::newRow("empty") << QString();
        QTest::newRow("paragraph") << u"Zwykły tekst"_s;
        QTest::newRow("header only") << u"| a |"_s;
    }
    void rejects()
    {
        QFETCH(QString, markdown);
        QVERIFY(!brulion::calc::parseTable(markdown));
    }

    void displayWidth_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("width");
        QTest::newRow("ascii") << u"abc"_s << 3;
        QTest::newRow("polish") << u"zażółć"_s << 6;
        QTest::newRow("decomposed") << u"ilos\u0301c\u0301"_s << 5;
        QTest::newRow("cjk") << u"日本語"_s << 6;
        QTest::newRow("coffee") << u"☕"_s << 2;
        QTest::newRow("alarm") << u"⏰ 9:00"_s << 7;
        QTest::newRow("family") << u"\U0001F468\u200D\U0001F469\u200D\U0001F467"_s << 2;
        QTest::newRow("flag") << u"\U0001F1F5\U0001F1F1"_s << 2;
        QTest::newRow("copyright") << u"©"_s << 1;
        QTest::newRow("heart vs16") << u"\u2764\uFE0F"_s << 2;
        QTest::newRow("zero width") << u"a\u200Bb"_s << 2;
        QTest::newRow("empty") << QString() << 0;
    }
    void displayWidth()
    {
        QFETCH(QString, text);
        QFETCH(int, width);
        QCOMPARE(brulion::calc::displayWidth(text), width);
    }

    void serializeAligned()
    {
        TableModel model;
        model.header = {u"Produkt"_s, u"Cena"_s, u"Ilość"_s, u"X"_s};
        model.aligns = {L, R, C, N};
        model.rows = {{u"Jabłka"_s, u"3,50 zł"_s, u"4"_s, QString()}, {u"Kawa ☕"_s, u"20 zł"_s, u"10"_s, u"x"_s}};
        QCOMPARE(serializeTable(model), QStringList({
                                            u"| Produkt |    Cena | Ilość | X   |"_s,
                                            u"| :------ | ------: | :---: | --- |"_s,
                                            u"| Jabłka  | 3,50 zł |   4   |     |"_s,
                                            u"| Kawa ☕ |   20 zł |  10   | x   |"_s,
                                        })
                                            .join(u'\n'));
    }

    void serializeEscapes()
    {
        TableModel model;
        model.header = {u"a|b"_s};
        model.aligns = {N};
        model.rows = {{u"x\ny"_s}, {u"p\r\nq"_s}};
        QCOMPARE(serializeTable(model), u"| a\\|b   |\n| ------ |\n| x<br>y |\n| p<br>q |"_s);
    }

    void serializeExtendsHeader()
    {
        TableModel model;
        model.header = {u"a"_s};
        model.rows = {{u"1"_s, u"2"_s}};
        QCOMPARE(serializeTable(model), u"| a   |     |\n| --- | --- |\n| 1   | 2   |"_s);
        QCOMPARE(serializeTable(TableModel()), QString());
    }

    void roundTrip()
    {
        TableModel model;
        model.header = {u"Zadanie"_s, u"Koszt | netto"_s, u"日本"_s};
        model.aligns = {N, R, C};
        model.rows = {{u"Malowanie"_s, u"1 200 zł"_s, u"=B1*2"_s}, {u"Żółć"_s, QString(), u"✅"_s}};
        const auto parsed = brulion::calc::parseTable(serializeTable(model));
        QVERIFY(parsed);
        QCOMPARE(*parsed, model);
        QCOMPARE(serializeTable(*brulion::calc::parseTable(serializeTable(model))), serializeTable(model));
    }

    void serializesManyRows()
    {
        TableModel model;
        model.header = {u"n"_s};
        model.aligns = {N};
        for (int i = 0; i < 100000; ++i)
            model.rows.append({QString::number(i)});
        const QString text = serializeTable(model);
        const QStringList lines = text.split(u'\n');
        QCOMPARE(lines.size(), 100002);
        QCOMPARE(lines.at(2), u"| 0     |"_s); // widest cell: "99999"
    }

    // --- findTables -----------------------------------------------------------------------------

    void findsTables()
    {
        const QString content = u"Tekst\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\npo"_s;
        const auto tables = findTables(content);
        QCOMPARE(tables.size(), 1);
        QCOMPARE(tables[0], (TableSpan{2, 4, 7, 36, QString()}));
        QCOMPARE(slice(content, tables[0]), u"| a | b |\n|---|---|\n| 1 | 2 |"_s);
    }

    void crlfOffsets()
    {
        const QString content = u"x\r\n| a |\r\n| - |\r\n| 1 |\r\n\r\nend"_s;
        const auto tables = findTables(content);
        QCOMPARE(tables.size(), 1);
        QCOMPARE(slice(content, tables[0]), u"| a |\r\n| - |\r\n| 1 |"_s);
        QCOMPARE(lineRanges(tables), (QList<QList<int>>{{1, 3}}));
    }

    void skipsCodeAndFrontmatter()
    {
        const QString content =
            u"---\na: | b |\n---\n```\n| a | b |\n|---|---|\n```\n~~~md\n| c |\n|---|\n~~~\n| real |\n|------|"_s;
        const auto tables = findTables(content);
        QCOMPARE(tables.size(), 1);
        QCOMPARE(tables[0].startLine, 11);
    }

    void indentedInListItems()
    {
        const auto tables = findTables(u"- lista\n  | x | y |\n  |---|---|\n  | 1 | 2 |\n- dalej");
        QCOMPARE(tables.size(), 1);
        QCOMPARE(tables[0].startLine, 1);
        QCOMPARE(tables[0].endLine, 3);
        QCOMPARE(tables[0].indent, u"  "_s);
    }

    void endsAtBlocks()
    {
        QCOMPARE(lineRanges(findTables(u"| a |\n|---|\n| 1 |\ntekst\n# Nagłówek\n| b |\n|---|\n- item")),
                 (QList<QList<int>>{{0, 3}, {5, 6}}));
        QCOMPARE(lineRanges(findTables(u"| a |\n|---|\n| 1 |\n> cytat")), (QList<QList<int>>{{0, 2}}));
        QCOMPARE(lineRanges(findTables(u"| a |\n|---|\n| 1 |\n***")), (QList<QList<int>>{{0, 2}}));
    }

    void severalTables()
    {
        QCOMPARE(lineRanges(findTables(u"| a |\n|---|\n\n| b | c |\n|:-|-:|\n| 1 | 2 |")),
                 (QList<QList<int>>{{0, 1}, {3, 5}}));
    }

    void notTables()
    {
        QVERIFY(findTables(u"Tytuł | x\n---").isEmpty());
        QVERIFY(findTables(u"a | b\n").isEmpty());
        QVERIFY(findTables(u"").isEmpty());
        QVERIFY(findTables(u"- a | b\n- c | d").isEmpty());
        QVERIFY(findTables(u"- | a |\n- |---|").isEmpty());
        QCOMPARE(findTables(u"1. | a | b |\n   |---|---|\n   | 1 | 2 |").size(), 1);
    }

    void logseqTables()
    {
        const QString content = u"- zakupy\n- | Produkt | Cena |\n  |---|---|\n  | Chleb | 4 zł |\n- dalej"_s;
        const auto tables = findTables(content);
        QCOMPARE(tables.size(), 1);
        QCOMPARE(tables[0].startLine, 1);
        QCOMPARE(tables[0].endLine, 3);
        QCOMPARE(tables[0].indent, u"  "_s);
        QCOMPARE(slice(content, tables[0]), u"| Produkt | Cena |\n  |---|---|\n  | Chleb | 4 zł |"_s);
        QCOMPARE(brulion::calc::parseTable(slice(content, tables[0]))->rows,
                 (QList<QStringList>{{u"Chleb"_s, u"4 zł"_s}}));
    }

    void tabIndentedListTables()
    {
        const QString content = u"- rodzic\n\t- | a | b |\n\t  | - | - |\n\t  | 1 | 2 |"_s;
        const auto tables = findTables(content);
        QCOMPARE(tables.size(), 1);
        QCOMPARE(tables[0].startLine, 1);
        QCOMPARE(tables[0].endLine, 3);
        QCOMPARE(tables[0].indent, u"\t  "_s);
        QCOMPARE(slice(content, tables[0]), u"| a | b |\n\t  | - | - |\n\t  | 1 | 2 |"_s);
    }

    void replaceKeepsIndentation()
    {
        const QString content = u"- lista\n  | x | y |\n  |---|---|\n  | 1 | 2 |"_s;
        const TableSpan t = findTables(content).at(0);
        QString pretty = serializeTable(*brulion::calc::parseTable(slice(content, t)));
        pretty.replace(u'\n', QString(u'\n' + t.indent));
        QCOMPARE(QString(content.left(t.from) + pretty + content.mid(t.to)),
                 u"- lista\n  | x   | y   |\n  | --- | --- |\n  | 1   | 2   |"_s);
        QCOMPARE(replaceTable(content, t, *brulion::calc::parseTable(slice(content, t))),
                 u"- lista\n  | x   | y   |\n  | --- | --- |\n  | 1   | 2   |"_s);
    }

    void replaceTableKeepsLineEndings()
    {
        // CRLF notes stay CRLF (the project preserves line endings); untouched text is kept.
        const QString crlf = u"Tekst\r\n| a | b |\r\n|---|---|\r\n| 1 | 2 |\r\n\r\nkoniec"_s;
        const TableSpan t = findTables(crlf).at(0);
        const TableModel model = *brulion::calc::parseTable(slice(crlf, t));
        QCOMPARE(replaceTable(crlf, t, model), u"Tekst\r\n| a   | b   |\r\n| --- | --- |\r\n| 1   | 2   |\r\n\r\nkoniec"_s);

        const QString item = u"- lista\r\n  | x | y |\r\n  |---|---|\r\n  | 1 | 2 |"_s;
        const TableSpan it = findTables(item).at(0);
        QCOMPARE(replaceTable(item, it, *brulion::calc::parseTable(slice(item, it))),
                 u"- lista\r\n  | x   | y   |\r\n  | --- | --- |\r\n  | 1   | 2   |"_s);

        const QString lf = u"- lista\n  | x | y |\n  |---|---|\n  | 1 | 2 |\n"_s;
        const TableSpan l = findTables(lf).at(0);
        TableModel edited = *brulion::calc::parseTable(slice(lf, l));
        edited.rows.append({u"żółw"_s, u"3"_s});
        QCOMPARE(replaceTable(lf, l, edited),
                 u"- lista\n  | x    | y   |\n  | ---- | --- |\n  | 1    | 2   |\n  | żółw | 3   |\n"_s);

        const QString cr = u"| a |\r|---|\r| 1 |"_s;
        QCOMPARE(replaceTable(cr, findTables(cr).at(0), *brulion::calc::parseTable(cr)), u"| a   |\r| --- |\r| 1   |"_s);
    }

    void columnHelpers()
    {
        const QList<std::pair<QString, int>> cases = {{u"A"_s, 0}, {u"Z"_s, 25}, {u"AA"_s, 26}, {u"AZ"_s, 51}, {u"BA"_s, 52}};
        for (const auto &[name, index] : cases) {
            QCOMPARE(columnIndex(name), index);
            QCOMPARE(columnName(index), name);
        }
        QCOMPARE(columnIndex(u"ab"), 27);
        QCOMPARE(cellName(0, 1), u"B1"_s);
        QCOMPARE(cellName(9, 27), u"AB10"_s);
    }

    void columnHelpersRejectInvalidInput()
    {
        QCOMPARE(columnIndex(u""), -1);
        QCOMPARE(columnIndex(u"A1"), -1);
        QCOMPARE(columnIndex(u"Ż"), -1);
        QCOMPARE(columnIndex(u"A B"), -1);
        QCOMPARE(columnIndex(u"ZZZZZZ"), 321272405);
        QCOMPARE(columnIndex(u"FXSHRXX"), INT_MAX);
        QCOMPARE(columnIndex(u"FXSHRXY"), -1); // would overflow an int
        QCOMPARE(columnIndex(QString(40, u'A')), -1);
        QCOMPARE(columnName(INT_MAX), u"FXSHRXX"_s);
        QCOMPARE(columnName(-1), QString());
        QCOMPARE(cellName(INT_MAX, 0), u"A2147483648"_s);
    }

    // --- rows and columns added, deleted and moved ------------------------------------------------

    void rowEdits()
    {
        TableModel m = shop();
        QVERIFY(insertTableRow(m, 1));
        QCOMPARE(m.rows.size(), 4);
        QCOMPARE(m.rows.at(0).at(0), u"Płytki"_s);
        QCOMPARE(m.rows.at(1), QStringList({QString(), QString(), QString()}));
        QCOMPARE(m.rows.at(2).at(0), u"Żółta farba"_s);
        QVERIFY(insertTableRow(m, 4)); // at the end
        QCOMPARE(m.rows.size(), 5);
        QVERIFY(removeTableRow(m, 4));
        QVERIFY(removeTableRow(m, 1));
        QCOMPARE(m, shop());
        QVERIFY(moveTableRow(m, 0, 2));
        QCOMPARE(m.rows.at(0).at(0), u"Żółta farba"_s);
        QCOMPARE(m.rows.at(2).at(0), u"Płytki"_s);
        QVERIFY(moveTableRow(m, 2, 0));
        QCOMPARE(m, shop());
    }

    void columnEdits()
    {
        TableModel m = shop();
        QVERIFY(insertTableColumn(m, 1, u"Sklep"_s));
        QCOMPARE(m.header, QStringList({u"Pozycja"_s, u"Sklep"_s, u"Cena"_s, u"Ilość"_s}));
        QCOMPARE(m.aligns, QList<TableAlign>({L, N, R, C}));
        QCOMPARE(m.rows.at(0), QStringList({u"Płytki"_s, QString(), u"89"_s, u"2"_s}));
        QVERIFY(removeTableColumn(m, 1));
        QCOMPARE(m, shop());
        QVERIFY(moveTableColumn(m, 2, 0));
        QCOMPARE(m.header, QStringList({u"Ilość"_s, u"Pozycja"_s, u"Cena"_s}));
        QCOMPARE(m.aligns, QList<TableAlign>({C, L, R}));
        QCOMPARE(m.rows.at(1), QStringList({u"1"_s, u"Żółta farba"_s, u"40"_s}));
        QVERIFY(moveTableColumn(m, 0, 2));
        QCOMPARE(m, shop());
    }

    void editsOutOfRangeChangeNothing()
    {
        TableModel m = shop();
        const TableModel before = m;
        QVERIFY(!insertTableRow(m, -1));
        QVERIFY(!insertTableRow(m, 4));
        QVERIFY(!removeTableRow(m, -1));
        QVERIFY(!removeTableRow(m, 3));
        QVERIFY(!moveTableRow(m, 0, 0));
        QVERIFY(!moveTableRow(m, -1, 0));
        QVERIFY(!moveTableRow(m, 0, 3));
        QVERIFY(!insertTableColumn(m, -1, u"x"_s));
        QVERIFY(!insertTableColumn(m, 4, u"x"_s));
        QVERIFY(!removeTableColumn(m, 3));
        QVERIFY(!moveTableColumn(m, 1, 1));
        QVERIFY(!moveTableColumn(m, 0, 3));
        QCOMPARE(m, before);
    }

    void theOnlyColumnStaysTheLastRowGoes()
    {
        TableModel m;
        m.header = {u"Jedna"_s};
        m.aligns = {N};
        m.rows = {{u"ą"_s}};
        QVERIFY(!removeTableColumn(m, 0));
        QVERIFY(removeTableRow(m, 0));
        QVERIFY(m.rows.isEmpty());
        const std::optional<TableModel> read = brulion::calc::parseTable(brulion::calc::serializeTable(m));
        QVERIFY(read.has_value()); // still a table: a header and its delimiter row
        QCOMPARE(read->header, m.header);
        QVERIFY(insertTableRow(m, 0));
        QCOMPARE(m.rows, QList<QStringList>({{QString()}}));
    }

    void shortRowsAreFilledBeforeAnEdit()
    {
        TableModel m;
        m.header = {u"A"_s, u"B"_s};
        m.rows = {{u"1"_s}};
        QVERIFY(insertTableColumn(m, 1, u"C"_s));
        QCOMPARE(m.rows.at(0), QStringList({u"1"_s, QString(), QString()}));
        QCOMPARE(m.aligns.size(), 3);
    }

    void formulasFollowTheirCells_data()
    {
        QTest::addColumn<int>("op");
        QTest::addColumn<int>("a");
        QTest::addColumn<int>("b");
        QTest::addColumn<QString>("formula");
        QTest::addColumn<QString>("expected");

        // The table has 3 columns; the formula stands in row 5, column C, away from every edit
        // (rows 1-4 are numbers), so only what it names changes.
        const auto row = [](const char *name, Op op, int a, int b, const QString &f, const QString &e) {
            QTest::newRow(name) << int(op) << a << b << f << e;
        };
        row("row inserted above a cell", Op::InsertRow, 1, 0, u"=A2*2"_s, u"=A3*2"_s);
        row("row inserted below a cell", Op::InsertRow, 2, 0, u"=A2*2"_s, u"=A2*2"_s);
        row("row inserted at a cell moves it", Op::InsertRow, 0, 0, u"=A1+B4"_s, u"=A2+B5"_s);
        row("row inserted inside a range grows it", Op::InsertRow, 1, 0, u"=sum(A1:A3)"_s, u"=sum(A1:A4)"_s);
        row("row inserted before a range moves it", Op::InsertRow, 0, 0, u"=sum(A1:A3)"_s, u"=sum(A2:A4)"_s);
        row("row inserted after a range: as written", Op::InsertRow, 3, 0, u"=sum( A1 : A3 )"_s, u"=sum( A1 : A3 )"_s);
        row("row and a whole column", Op::InsertRow, 0, 0, u"=sum(A:B)+1"_s, u"=sum(A:B)+1"_s);
        row("row deleted above a cell", Op::RemoveRow, 0, 0, u"=A3"_s, u"=A2"_s);
        row("the cell's row deleted", Op::RemoveRow, 1, 0, u"=1+A2*B3"_s, u"=1+REF!*B2"_s);
        row("row deleted inside a range", Op::RemoveRow, 1, 0, u"=sum(A1:A3)"_s, u"=sum(A1:A2)"_s);
        row("a range's first row deleted", Op::RemoveRow, 0, 0, u"=sum(A1:A3)"_s, u"=sum(A1:A2)"_s);
        row("a range's last row deleted", Op::RemoveRow, 2, 0, u"=sum(A1:A3)"_s, u"=sum(A1:A2)"_s);
        row("a range of one row deleted", Op::RemoveRow, 1, 0, u"=sum(A2:B2)"_s, u"=sum(REF!)"_s);
        row("a row moved: a cell follows", Op::MoveRow, 0, 2, u"=A1+A2+A3+A4"_s, u"=A3+A1+A2+A4"_s);
        row("a row moved up: a cell follows", Op::MoveRow, 2, 0, u"=A1+A2+A3+A4"_s, u"=A2+A3+A1+A4"_s);
        row("a row moved: a range over several stays", Op::MoveRow, 0, 1, u"=sum(A1:A3)"_s, u"=sum(A1:A3)"_s);
        row("a row moved: a range in it follows", Op::MoveRow, 0, 1, u"=sum(A1:B1)"_s, u"=sum(A2:B2)"_s);
        row("column inserted", Op::InsertColumn, 1, 0, u"=A1+B1+[Cena]"_s, u"=A1+C1+[Cena]"_s);
        row("column inserted inside a range", Op::InsertColumn, 1, 0, u"=sum(A1:B2)"_s, u"=sum(A1:C2)"_s);
        row("column inserted: a whole column", Op::InsertColumn, 0, 0, u"=sum(B:B)"_s, u"=sum(C:C)"_s);
        row("column deleted", Op::RemoveColumn, 0, 0, u"=A1+B1"_s, u"=REF!+A1"_s);
        row("a whole column deleted", Op::RemoveColumn, 1, 0, u"=sum(B:B)+sum(A:B)"_s, u"=sum(REF!)+sum(A:A)"_s);
        row("a column moved: all follow", Op::MoveColumn, 0, 1, u"=A1+B1+sum(A:A)+sum(B1:B2)"_s,
            u"=B1+A1+sum(B:B)+sum(A1:A2)"_s);
        row("a column moved: a range over several stays", Op::MoveColumn, 0, 1, u"=sum(A1:B2)"_s, u"=sum(A1:B2)"_s);

        row("a reversed range is put in order", Op::InsertRow, 0, 0, u"=sum(A3:A1)"_s, u"=sum(A2:A4)"_s);
        row("already deleted stays deleted", Op::InsertRow, 0, 0, u"=REF!+A1"_s, u"=REF!+A2"_s);
        row("a string is no reference", Op::InsertRow, 0, 0, uR"(=if(A1 > 0, "A1", "ą"))"_s, uR"(=if(A2 > 0, "A1", "ą"))"_s);
        row("a function is no reference", Op::InsertRow, 0, 0, u"=LOG10(A1)"_s, u"=LOG10(A2)"_s);
        row("lower case is no reference", Op::InsertRow, 0, 0, u"=a1*2"_s, u"=a1*2"_s);
        row("a header named like a cell", Op::InsertRow, 0, 0, u"=[D1]+A1"_s, u"=[D1]+A2"_s);
        row("relative words stay", Op::InsertRow, 0, 0, u"=sum(above)+left"_s, u"=sum(above)+left"_s);
        row("a comment stays", Op::InsertRow, 0, 0, u"=A1 # A1 tu"_s, u"=A2 # A1 tu"_s);
        row("up to a syntax error", Op::InsertRow, 0, 0, u"=A1+"_s, u"=A2+"_s);
        row("decimal comma stays", Op::InsertRow, 0, 0, u"=A1*1,5"_s, u"=A2*1,5"_s);
        row("Polish variable next to a cell", Op::InsertRow, 0, 0, u"=A1*cenaŻ"_s, u"=A2*cenaŻ"_s);
    }

    void formulasFollowTheirCells()
    {
        QFETCH(int, op);
        QFETCH(int, a);
        QFETCH(int, b);
        QFETCH(QString, formula);
        QFETCH(QString, expected);

        TableModel m;
        m.header = {u"Cena"_s, u"D1"_s, u"Razem"_s};
        m.aligns = {N, N, N};
        for (int r = 0; r < 4; ++r)
            m.rows.append({QString::number(r + 1), QString::number(10 * (r + 1)), QString()});
        m.rows.append({QString(), QString(), formula});
        // The formula's own place, after the edit (a column edit in the last column would touch it).
        int formulaRow = 4;
        int formulaColumn = 2;
        switch (static_cast<Op>(op)) {
        case Op::InsertRow:
            QVERIFY(insertTableRow(m, a));
            ++formulaRow;
            break;
        case Op::RemoveRow:
            QVERIFY(removeTableRow(m, a));
            --formulaRow;
            break;
        case Op::MoveRow:
            QVERIFY(moveTableRow(m, a, b));
            break;
        case Op::InsertColumn:
            QVERIFY(insertTableColumn(m, a, u"Nowa"_s));
            ++formulaColumn;
            break;
        case Op::RemoveColumn:
            QVERIFY(removeTableColumn(m, a));
            --formulaColumn;
            break;
        case Op::MoveColumn:
            QVERIFY(moveTableColumn(m, a, b));
            break;
        }
        QCOMPARE(m.rows.at(formulaRow).at(formulaColumn), expected);
    }

    void resultsStayTheSameAfterEdits()
    {
        TableModel m;
        m.header = {u"Cena"_s, u"Ilość"_s, u"Razem"_s};
        m.aligns = {N, N, N};
        m.rows = {{u"10"_s, u"2"_s, u"=A1*B1"_s},
                  {u"5"_s, u"3"_s, u"=A2*B2"_s},
                  {QString(), QString(), u"=sum(C1:C2)"_s}};
        const auto total = [](const TableModel &t, int row, int column) {
            return evaluateTable(t).values.at(row).at(column);
        };
        QCOMPARE(total(m, 2, 2), u"35"_s);
        QVERIFY(insertTableRow(m, 1));
        QCOMPARE(m.rows.at(2).at(2), u"=A3*B3"_s);
        QCOMPARE(total(m, 3, 2), u"35"_s);
        QVERIFY(moveTableRow(m, 0, 2)); // over the new row and the second one: inside the sum
        QCOMPARE(total(m, 3, 2), u"35"_s);
        QVERIFY(insertTableColumn(m, 0, u"Nowa"_s));
        QCOMPARE(total(m, 3, 3), u"35"_s);
        QVERIFY(moveTableColumn(m, 0, 2));
        QCOMPARE(total(m, 3, 3), u"35"_s);
    }

    void aDeletedReferenceShowsAnError()
    {
        TableModel m;
        m.header = {u"Cena"_s, u"Razem"_s};
        m.aligns = {N, N};
        m.rows = {{u"10"_s, QString()}, {u"5"_s, u"=A1+A2"_s}};
        QVERIFY(removeTableRow(m, 0));
        QCOMPARE(m.rows.at(0).at(1), u"=REF!+A1"_s);
        const TableEval eval = evaluateTable(m, CalcLocale::Polish);
        QCOMPARE(eval.values.at(0).at(1), kErrorMarker.toString());
        QCOMPARE(eval.errors.at(0).at(1), u"Odwołanie do usuniętego wiersza lub kolumny"_s);
        QCOMPARE(evaluateTable(m, CalcLocale::English).errors.at(0).at(1), u"Reference to a deleted row or column"_s);
    }

    void aPageVariableCalledRefStaysAVariable()
    {
        TableModel m;
        m.header = {u"A"_s};
        m.aligns = {N};
        m.rows = {{u"=REF*2"_s}, {u"=REF!"_s}};
        Scope scope;
        scope.set(u"REF"_s, Value::number(3));
        const TableEval eval = evaluateTable(m, CalcLocale::Polish, scope);
        QCOMPARE(eval.values.at(0).at(0), u"6"_s);
        QCOMPARE(eval.values.at(1).at(0), kErrorMarker.toString());
    }
};

QTEST_GUILESS_MAIN(TestCalcTable)
#include "tst_calc_table.moc"
