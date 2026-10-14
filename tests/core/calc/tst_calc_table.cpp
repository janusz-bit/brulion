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
};

QTEST_GUILESS_MAIN(TestCalcTable)
#include "tst_calc_table.moc"
