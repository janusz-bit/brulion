// Calculations across edits in the block editor (BlockListModel + PageController,
// docs/SYNTAX.md §9): calc blocks, table formulas and inline calculations see the variables
// defined above them (calc blocks, `key:: value` properties, numeric frontmatter), and after any
// edit they show exactly what a model computing everything from scratch shows for the same
// blocks. The model keeps results across edits that cannot change what a calculation sees, so
// these tests edit definitions, the text between them and the calculations, blocks that lie
// inside a calc fence only as the calc module reads it (also after a BOM), the frontmatter, and
// finally run random edits (LF, CRLF, BOM pages) checked against such a model.
#include "blocklistmodel.h"
#include "fakeeditorhost.h"
#include "pagecontroller.h"

#include "calc/calc.h"

#include <QJsonDocument>
#include <QRegularExpression>

#include <QRandomGenerator>
#include <QTest>

using namespace brulion;
using namespace brulion::editor;
using namespace Qt::StringLiterals;

namespace {

const QString Page = u"Notatki/Obliczenia.md"_s;

struct Fixture {
    FakeEditorHost host;
    PageController page;

    explicit Fixture(const QString &content)
    {
        host.setFile(Page, content);
        page.setNow(u"2026-10-15 10:00"_s);
        page.setSaveDelay(0);
        page.setHost(&host);
        page.setPath(Page);
    }

    BlockListModel *model() const { return page.blocks(); }
    int count() const { return model()->rowCount(); }
    QVariant role(int row, int role) const { return model()->data(model()->index(row), role); }
    QString edit(int row) const { return role(row, BlockListModel::EditTextRole).toString(); }
    QString view(int row) const { return role(row, BlockListModel::ViewRole).toString(); }
    /// First row whose edit text starts with `prefix`, -1 when none.
    int rowOf(const QString &prefix) const
    {
        for (int r = 0; r < count(); ++r) {
            if (edit(r).startsWith(prefix))
                return r;
        }
        return -1;
    }
    /// The n-th table row (0-based) of the page.
    int table(int n = 0) const
    {
        for (int r = 0; r < count(); ++r) {
            if (view(r) == u"table" && n-- == 0)
                return r;
        }
        return -1;
    }
    /// Displayed value of body cell [tableRow][column] of the table in `row`.
    QString cell(int row, int tableRow, int column) const
    {
        const QVariantList rows = role(row, BlockListModel::TableDataRole).toMap().value(u"rows"_s).toList();
        if (tableRow >= rows.size())
            return u"<no row>"_s;
        const QVariantList cells = rows.at(tableRow).toList();
        return column < cells.size() ? cells.at(column).toMap().value(u"display"_s).toString() : u"<no cell>"_s;
    }
    /// Error message of body cell [tableRow][column] of the table in `row`.
    QString cellError(int row, int tableRow, int column) const
    {
        const QVariantList rows = role(row, BlockListModel::TableDataRole).toMap().value(u"rows"_s).toList();
        if (tableRow >= rows.size() || column >= rows.at(tableRow).toList().size())
            return u"<no cell>"_s;
        return rows.at(tableRow).toList().at(column).toMap().value(u"error"_s).toString();
    }
    QString calcValue(int row, int line) const
    {
        const QVariantList results = role(row, BlockListModel::CalcResultsRole).toList();
        return line < results.size() ? results.at(line).toMap().value(u"value"_s).toString() : u"<no line>"_s;
    }
    QString html(int row) const { return role(row, BlockListModel::HtmlRole).toString(); }

    /// Every calculation of the page equals what a model computing everything from scratch shows
    /// for the same blocks (a copy of the document: a re-parse of the text may split blocks
    /// differently after some edits), and table / calc results equal calc::PageCalc's scope at
    /// the block's line used directly.
    bool matchesFreshModel(QString *why)
    {
        BlockListModel fresh;
        fresh.setHost(&host);
        fresh.setPath(Page);
        fresh.setToday(model()->today());
        fresh.setEnglish(model()->english());
        fresh.setPalette(model()->palette());
        fresh.resetContent(QString());
        const markdown::BlockDocument copy = model()->document();
        fresh.apply([&](markdown::BlockDocument &d) {
            using Type = markdown::BlockChange::Type;
            markdown::EditResult r;
            if (d.count() > 0)
                r.changes.append({Type::Removed, 0, d.count(), -1});
            d = copy;
            if (d.count() > 0)
                r.changes.append({Type::Inserted, 0, d.count(), -1});
            return r;
        });
        if (fresh.rowCount() != count() || fresh.text() != page.text()) {
            *why = u"rows: %1 vs fresh %2"_s.arg(count()).arg(fresh.rowCount());
            return false;
        }
        const QString text = page.text();
        const calc::CalcLocale locale = model()->english() ? calc::CalcLocale::English : calc::CalcLocale::Polish;
        const calc::PageCalc pageCalc(text, locale);
        const auto fail = [&](int r, const QString &what, const QString &mine, const QString &theirs) {
            *why = u"row %1 (%2) %3:\n%4\nexpected:\n%5\npage:\n%6"_s.arg(r).arg(edit(r).left(60), what, mine, theirs,
                                                                                 text);
            return false;
        };
        for (int r = 0; r < count(); ++r) {
            for (const int role : {int(BlockListModel::CalcResultsRole), int(BlockListModel::TableDataRole),
                                   int(BlockListModel::HtmlRole)}) {
                const QVariant mine = this->role(r, role);
                const QVariant theirs = fresh.data(fresh.index(r), role);
                if (mine != theirs) {
                    return fail(r, QString::fromLatin1(model()->roleNames().value(role)),
                                QString::fromUtf8(QJsonDocument::fromVariant(QVariantList{mine}).toJson()),
                                QString::fromUtf8(QJsonDocument::fromVariant(QVariantList{theirs}).toJson()));
                }
            }
            const markdown::BlockEntry &e = model()->document().at(r);
            if (view(r) == u"table") {
                const auto table = calc::parseTable(e.body);
                if (!table)
                    continue;
                const calc::TableEval eval = calc::evaluateTable(*table, locale, pageCalc.scopeAt(e.line));
                for (qsizetype tr = 0; tr < table->rows.size(); ++tr) {
                    for (qsizetype c = 0; c < table->header.size(); ++c) {
                        const QString raw = c < table->rows[tr].size() ? table->rows[tr][c] : QString();
                        if (!raw.trimmed().startsWith(u'='))
                            continue;
                        if (cell(r, int(tr), int(c)) != eval.values[tr][c])
                            return fail(r, u"cell %1,%2"_s.arg(tr).arg(c), cell(r, int(tr), int(c)),
                                        eval.values[tr][c]);
                    }
                }
            } else if (view(r) == u"calc") {
                calc::Scope scope = pageCalc.scopeAt(e.line);
                const QList<calc::CalcLineResult> results = calc::evaluateCalc(edit(r), scope, locale);
                for (qsizetype l = 0; l < results.size(); ++l) {
                    if (calcValue(r, int(l)) != results[l].value)
                        return fail(r, u"calc line %1"_s.arg(l), calcValue(r, int(l)), results[l].value);
                }
            }
        }
        return true;
    }
};

#define VERIFY_FRESH(f)                                                                                                \
    do {                                                                                                               \
        QString why_;                                                                                                  \
        QVERIFY2((f).matchesFreshModel(&why_), qPrintable(why_));                                                      \
    } while (false)

const QString Shop = uR"(```calc
cena = 10
ilość = 3
```

Tekst pomiędzy, zażółć gęślą jaźń

| Rzecz | Kwota |
| --- | ---: |
| Farba | =cena * ilość |
| Pędzel | 12,5 |
| Razem | =sum(above) |

Wynik: `= cena * 2`
)"_s;

} // namespace

class TestEditorCalc : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void variablesFromEarlierBlocks()
    {
        Fixture f(Shop);
        const int t = f.table();
        QVERIFY(t > 0);
        QCOMPARE(f.cell(t, 0, 1), u"30"_s);
        QCOMPARE(f.cell(t, 2, 1), u"42,5"_s);
        const int inlineRow = f.rowOf(u"Wynik:"_s);
        QVERIFY2(f.html(inlineRow).contains(u"20"_s), qPrintable(f.html(inlineRow)));

        // Typing between the definitions and the table: nothing changes.
        const int between = f.rowOf(u"Tekst pomiędzy"_s);
        f.page.setEditText(between, f.edit(between) + u" i więcej ąę"_s);
        QCOMPARE(f.cell(t, 0, 1), u"30"_s);
        VERIFY_FRESH(f);

        // The definition changes: the table and the inline calculation below follow.
        f.page.setEditText(0, u"cena = 20\nilość = 3"_s);
        QCOMPARE(f.calcValue(0, 0), u"20"_s);
        QCOMPARE(f.cell(t, 0, 1), u"60"_s);
        QCOMPARE(f.cell(t, 2, 1), u"72,5"_s);
        QVERIFY2(f.html(inlineRow).contains(u"40"_s), qPrintable(f.html(inlineRow)));
        VERIFY_FRESH(f);

        // A definition appears between the calc block and the table (a property overrides).
        const int at = f.page.insertBlockAfter(between, u"ilość:: 5"_s);
        QVERIFY(at > between);
        QCOMPARE(f.cell(f.table(), 0, 1), u"100"_s);
        VERIFY_FRESH(f);
        // …its value is edited, then it goes away again.
        f.page.setEditText(at, u"ilość:: 7"_s);
        QCOMPARE(f.cell(f.table(), 0, 1), u"140"_s);
        VERIFY_FRESH(f);
        f.page.deleteBlock(at);
        QCOMPARE(f.cell(f.table(), 0, 1), u"60"_s);
        VERIFY_FRESH(f);

        f.page.undo();
        QCOMPARE(f.cell(f.table(), 0, 1), u"140"_s);
        VERIFY_FRESH(f);
        f.page.redo();
        QCOMPARE(f.cell(f.table(), 0, 1), u"60"_s);
        VERIFY_FRESH(f);
    }

    void sumAboveFollowsTheTableItself()
    {
        Fixture f(Shop);
        const int t = f.table();
        f.page.setTableCell(t, 1, 1, u"20"_s, true); // typing into a cell
        QCOMPARE(f.cell(t, 2, 1), u"50"_s);
        f.page.setTableCell(t, 1, 1, u"200"_s, true);
        QCOMPARE(f.cell(t, 2, 1), u"230"_s);
        VERIFY_FRESH(f);
        f.page.addTableRow(t);
        f.page.setTableCell(t, 3, 0, u"Wałek"_s);
        f.page.setTableCell(t, 3, 1, u"=cena + 1"_s);
        QCOMPARE(f.cell(t, 3, 1), u"11"_s);
        VERIFY_FRESH(f);
    }

    void inlineCalculationInATableCell_data()
    {
        QTest::addColumn<QString>("table");
        // Only inline calculations: nothing else in the table reads calc variables.
        QTest::addRow("inline only") << u"| Rzecz `= cena` | Kwota |\n| --- | --- |\n| Farba | `= cena * 2` zł |\n"_s;
        // Next to a formula: the table is recomputed, the cell must not come from a cache.
        QTest::addRow("with a formula")
            << u"| Rzecz `= cena` | Kwota |\n| --- | --- |\n| Farba | `= cena * 2` zł |\n| Razem | =cena + 1 |\n"_s;
        QTest::addRow("spaced, CRLF")
            << u"| Rzecz ` = cena` | Kwota |\r\n| --- | --- |\r\n| Farba | ` = cena * 2` zł |\r\n"_s;
        // Any whitespace may stand between the backtick and `=` (InlineRenderer trims it).
        QTest::addRow("two spaces") << u"| Rzecz `  = cena` | Kwota |\n| --- | --- |\n| Farba | `  = cena * 2` zł |\n"_s;
        QTest::addRow("tab") << u"| Rzecz `\t= cena` | Kwota |\n| --- | --- |\n| Farba | `\t= cena * 2` zł |\n"_s;
    }

    void inlineCalculationInATableCell()
    {
        // `= …` inline code inside a cell is rendered with the variables above the table, and
        // follows when they change (the cell's text itself stays the same).
        QFETCH(QString, table);
        Fixture f(u"```calc\ncena = 10\n```\n\nŻółta notatka\n\n"_s + table);
        // Shown text (no markup: colours in the chip's style are digits too).
        const auto cellText = [&f](int row, int tableRow, int column) {
            const QVariantList rows = f.role(row, BlockListModel::TableDataRole).toMap().value(u"rows"_s).toList();
            return rows.value(tableRow).toList().value(column).toMap().value(u"plain"_s).toString();
        };
        const auto headerText = [&f](int row, int column) {
            QString html =
                    f.role(row, BlockListModel::TableDataRole).toMap().value(u"headerHtml"_s).toList().value(column).toString();
            return html.remove(QRegularExpression(u"<[^>]*>"_s));
        };
        int t = f.table();
        QVERIFY(t > 0);
        QVERIFY2(cellText(t, 0, 1).contains(u"20"_s), qPrintable(cellText(t, 0, 1)));
        QVERIFY2(headerText(t, 0).contains(u"10"_s), qPrintable(headerText(t, 0)));

        // Typing in the note between them: nothing changes.
        const int note = f.rowOf(u"Żółta"_s);
        f.page.setEditText(note, u"Żółta notatka ąćęłńóśźż"_s);
        QVERIFY2(cellText(t, 0, 1).contains(u"20"_s), qPrintable(cellText(t, 0, 1)));
        VERIFY_FRESH(f);

        // The definition changes: the cell and the header follow.
        f.page.setEditText(0, u"cena = 21"_s);
        QVERIFY2(cellText(t, 0, 1).contains(u"42"_s), qPrintable(cellText(t, 0, 1)));
        QVERIFY2(headerText(t, 0).contains(u"21"_s), qPrintable(headerText(t, 0)));
        VERIFY_FRESH(f);

        // …is removed (an error), and comes back with undo (typing in one block is one step).
        f.page.setEditText(0, u"inna = 1"_s);
        QVERIFY2(cellText(t, 0, 1).contains(u"#ERR"_s), qPrintable(cellText(t, 0, 1)));
        VERIFY_FRESH(f);
        f.page.undo();
        QCOMPARE(f.edit(0), u"cena = 10"_s);
        QVERIFY2(cellText(t, 0, 1).contains(u"20"_s), qPrintable(cellText(t, 0, 1)));
        QVERIFY2(headerText(t, 0).contains(u"10"_s), qPrintable(headerText(t, 0)));
        VERIFY_FRESH(f);
        f.page.redo();
        QVERIFY2(cellText(t, 0, 1).contains(u"#ERR"_s), qPrintable(cellText(t, 0, 1)));
        VERIFY_FRESH(f);
        f.page.setEditText(0, u"cena = 21"_s);

        // A property between them overrides; editing the cell itself still works.
        const int at = f.page.insertBlockAfter(note, u"cena:: 3"_s);
        QVERIFY(at > note);
        t = f.table();
        QVERIFY2(cellText(t, 0, 1).contains(u"6"_s), qPrintable(cellText(t, 0, 1)));
        VERIFY_FRESH(f);
        f.page.setTableCell(t, 0, 1, u"`= cena * 5` zł"_s, true);
        QVERIFY2(cellText(t, 0, 1).contains(u"15"_s), qPrintable(cellText(t, 0, 1)));
        VERIFY_FRESH(f);
    }

    void blockReferenceInATableCell_data()
    {
        QTest::addColumn<QString>("cell");
        QTest::addRow("block ref") << u"((farba-1))"_s;
        QTest::addRow("same-page link") << u"[[#^farba-1]]"_s;
        QTest::addRow("next to a formula") << u"((farba-1))"_s; // the table also has a formula row
    }

    void blockReferenceInATableCell()
    {
        // Old bug: a table cell was cached by its text, so a reference showed the referenced
        // block's old text after that block was edited (a paragraph follows it).
        QFETCH(QString, cell);
        QString content = u"Żółta farba ^farba-1\n\n| Rzecz | Uwagi |\n| --- | --- |\n| Farba | "_s + cell
                + u" |\n"_s;
        if (QByteArrayView(QTest::currentDataTag()) == "next to a formula")
            content += u"| Razem | =1 + 1 |\n"_s;
        Fixture f(content);
        const auto shown = [&f](int row) {
            const QVariantList rows = f.role(row, BlockListModel::TableDataRole).toMap().value(u"rows"_s).toList();
            QString html = rows.value(0).toList().value(1).toMap().value(u"html"_s).toString();
            return html.remove(QRegularExpression(u"<[^>]*>"_s));
        };
        const int t = f.table();
        QVERIFY(t > 0);
        QVERIFY2(shown(t).contains(u"Żółta farba"_s), qPrintable(shown(t)));

        f.page.setEditText(0, u"Zielona farba ąćęłńóśźż ^farba-1"_s);
        QVERIFY2(shown(t).contains(u"Zielona farba ąćęłńóśźż"_s), qPrintable(shown(t)));
        QVERIFY2(!shown(t).contains(u"Żółta"_s), qPrintable(shown(t)));
        VERIFY_FRESH(f);

        f.page.undo();
        QCOMPARE(f.edit(0), u"Żółta farba ^farba-1"_s);
        QVERIFY2(shown(t).contains(u"Żółta farba"_s), qPrintable(shown(t)));
        VERIFY_FRESH(f);
        f.page.redo();
        QVERIFY2(shown(t).contains(u"Zielona farba"_s), qPrintable(shown(t)));
        VERIFY_FRESH(f);
    }

    void inlineCalculationAfterALineBreak_data()
    {
        QTest::addColumn<QString>("block");
        // The code span's text starts on the next line of the block: a line break (rendered as
        // a space) before `=` still makes it an inline calculation.
        QTest::addRow("LF") << u"Wynik: `\n= cena * 2` zł"_s;
        QTest::addRow("CRLF") << u"Wynik: `\r\n= cena * 2` zł"_s;
        QTest::addRow("indented line") << u"Wynik: `\n   = cena * 2` zł"_s;
        QTest::addRow("double backticks") << u"Wynik: ``\n= cena * 2 `` zł"_s;
        QTest::addRow("tab") << u"Wynik: `\t= cena * 2` zł"_s;
        QTest::addRow("two spaces") << u"Wynik: `  = cena * 2` zł"_s;
        // In a quote or callout the next line starts with its `> ` prefix, which the renderer
        // strips before the inline text.
        QTest::addRow("quote") << u"> Wynik: `\n> = cena * 2` zł"_s;
        QTest::addRow("nested quote") << u"> > Wynik: `\n> >   = cena * 2` zł"_s;
        QTest::addRow("quote, lazy line") << u"> Wynik: `\n= cena * 2` zł"_s;
        QTest::addRow("callout") << u"> [!note] Koszt\n> Wynik: `\n> = cena * 2` zł"_s;
        QTest::addRow("callout, CRLF") << u"> [!note] Koszt\r\n> Wynik: `\r\n>= cena * 2` zł"_s;
    }

    void inlineCalculationAfterALineBreak()
    {
        // Old bug: such a block was rendered with the variables once and never again when
        // they changed (the model did not see that its html depends on the calc scope).
        QFETCH(QString, block);
        QString content = u"```calc\ncena = 10\n```\n\nŻółć pośrodku\n\n"_s + block + u"\n"_s;
        if (block.contains(u'\r'))
            content.replace(u"\n"_s, u"\r\n"_s).replace(u"\r\r\n"_s, u"\r\n"_s);
        Fixture f(content);
        const auto shown = [&f](int row) { return f.html(row).remove(QRegularExpression(u"<[^>]*>"_s)); };
        const auto rowOfResult = [&f] {
            for (int r = 0; r < f.count(); ++r) {
                if (f.edit(r).contains(u"Wynik:"_s))
                    return r;
            }
            return -1;
        };
        const int row = rowOfResult();
        QVERIFY(row > 0);
        QVERIFY2(shown(row).contains(u"20"_s), qPrintable(f.html(row)));

        f.page.setEditText(0, u"cena = 21"_s);
        QVERIFY2(shown(row).contains(u"42"_s), qPrintable(f.html(row)));
        VERIFY_FRESH(f);

        f.page.setEditText(0, u"inna = 1"_s);
        QVERIFY2(shown(row).contains(u"#ERR"_s), qPrintable(f.html(row)));
        VERIFY_FRESH(f);
        f.page.undo();
        QVERIFY2(shown(row).contains(u"20"_s), qPrintable(f.html(row)));
        VERIFY_FRESH(f);

        // A property between them overrides.
        const int at = f.page.insertBlockAfter(f.rowOf(u"Żółć"_s), u"cena:: 3"_s);
        QVERIFY(at > 0);
        QVERIFY2(shown(rowOfResult()).contains(u"6"_s), qPrintable(f.html(rowOfResult())));
        VERIFY_FRESH(f);
    }

    void propertyAboveATableIsEdited()
    {
        // `key:: value` properties in list items, a table below and one above the definition.
        Fixture f(u"| Przed |\n| --- |\n| =stawka * 2 |\n\n- Projekt\n  - stawka:: 25\n  - Notatka\n\n"
                  u"| Po | Godziny |\n| --- | --- |\n| =stawka * [Godziny] | 4 |\n"_s);
        const int above = f.table(0);
        const int below = f.table(1);
        QCOMPARE(f.cell(below, 0, 0), u"100"_s);
        QVERIFY(!f.cellError(above, 0, 0).isEmpty()); // defined below it: unknown there
        const int prop = f.rowOf(u"stawka::"_s);
        f.page.setEditText(prop, u"stawka:: 30"_s);
        QCOMPARE(f.cell(below, 0, 0), u"120"_s);
        VERIFY_FRESH(f);
        // Typing in the note under it, Enter, a new note: nothing changes.
        const int note = f.rowOf(u"Notatka"_s);
        f.page.setEditText(note, u"Notatka z kosztami"_s);
        f.page.split(note, 7);
        QCOMPARE(f.cell(f.table(1), 0, 0), u"120"_s);
        VERIFY_FRESH(f);
        // The property moves below the table (Alt+Down past it is not possible: delete + add).
        f.page.deleteBlock(f.rowOf(u"stawka::"_s));
        QVERIFY(!f.cellError(f.table(1), 0, 0).isEmpty());
        VERIFY_FRESH(f);
        f.page.undo();
        QCOMPARE(f.cell(f.table(1), 0, 0), u"120"_s);
        VERIFY_FRESH(f);
    }

    void frontmatterVariables()
    {
        Fixture f(u"---\nbudżet: 1200\nstatus: w toku\n---\n\nAkapit\n\n| Zostaje |\n| --- |\n| =budżet - 200 |\n"_s);
        const int t = f.table();
        QCOMPARE(f.cell(t, 0, 0), u"1000"_s);
        f.page.setEditText(f.rowOf(u"Akapit"_s), u"Akapit zmieniony"_s);
        QCOMPARE(f.cell(t, 0, 0), u"1000"_s);
        VERIFY_FRESH(f);
        f.page.setBody(0, u"---\nbudżet: 1500\nstatus: w toku\n---"_s);
        QCOMPARE(f.cell(f.table(), 0, 0), u"1300"_s);
        VERIFY_FRESH(f);
    }

    void frontmatterOnlyTheCalcModuleSees()
    {
        // An indented "---" is a rule for Markdown, but opens frontmatter for the calc module
        // (up to the "..." line): the paragraph "cena: 5" is a frontmatter entry there.
        Fixture f(u"  ---\n\ncena: 5\n\n...\n\nTekst\n\n| W |\n| --- |\n| =cena * 2 |\n"_s);
        const int t = f.table();
        QCOMPARE(f.cell(t, 0, 0), u"10"_s);
        const int entry = f.rowOf(u"cena: 5"_s);
        QVERIFY(entry > 0);
        QCOMPARE(f.view(entry), u"text"_s);
        f.page.setEditText(entry, u"cena: 6"_s);
        QCOMPARE(f.cell(f.table(), 0, 0), u"12"_s);
        VERIFY_FRESH(f);
        f.page.setEditText(f.rowOf(u"Tekst"_s), u"Tekst dalej"_s);
        QCOMPARE(f.cell(f.table(), 0, 0), u"12"_s);
        VERIFY_FRESH(f);
    }

    void blockInsideACalcFenceOnlyTheCalcModuleSees()
    {
        // The list item's fence ends with the item for Markdown, but the calc module reads its
        // calc block up to the next ``` line: the paragraph "b = 2" is part of it.
        Fixture f(u"- ```calc\n  a = 1\nb = 2\n\n~~~\n```\n~~~\n\nTekst\n\n| Wynik |\n| --- |\n| =a + b * 10 |\n"_s);
        const int t = f.table();
        QVERIFY(t > 0);
        QCOMPARE(f.cell(t, 0, 0), u"21"_s);
        const int b = f.rowOf(u"b = 2"_s);
        QVERIFY(b > 0);
        QCOMPARE(f.view(b), u"text"_s);
        f.page.setEditText(b, u"b = 3"_s); // plain text for Markdown, a definition for calc
        QCOMPARE(f.cell(f.table(), 0, 0), u"31"_s);
        VERIFY_FRESH(f);
        // A new block right after it lands inside that calc block too.
        const int c = f.page.insertBlockAfter(f.rowOf(u"b = 3"_s), u"a = 5"_s);
        QVERIFY(c > b);
        QCOMPARE(f.cell(f.table(), 0, 0), u"35"_s);
        VERIFY_FRESH(f);
        // Text after the fences is outside: typing there changes nothing.
        f.page.setEditText(f.rowOf(u"Tekst"_s), u"Tekst dalej"_s);
        QCOMPARE(f.cell(f.table(), 0, 0), u"35"_s);
        VERIFY_FRESH(f);
    }

    void listItemsInsideACalcFence()
    {
        // The same with list items (no blank lines between them): "- 7" is the calc line "-7",
        // the last result (`ans`); the first ``` line closes the calc block, the second opens a
        // fence Markdown closes right away.
        Fixture f(u"- ```calc\n  a = 1\n- 7\n```\n```\n\n| W |\n| --- |\n| =ans * a |\n"_s);
        QCOMPARE(f.cell(f.table(), 0, 0), u"-7"_s);
        const int seven = f.rowOf(u"7"_s);
        QVERIFY(seven > 0);
        // A new item right after it: inside the calc block, its last line now.
        const int eight = f.page.insertBlockAfter(seven, u"8"_s);
        QCOMPARE(eight, seven + 1);
        QCOMPARE(f.cell(f.table(), 0, 0), u"-8"_s);
        VERIFY_FRESH(f);
        f.page.setEditText(eight, u"9"_s);
        QCOMPARE(f.cell(f.table(), 0, 0), u"-9"_s);
        VERIFY_FRESH(f);
        f.page.deleteBlock(eight);
        QCOMPARE(f.cell(f.table(), 0, 0), u"-7"_s);
        VERIFY_FRESH(f);
    }

    void byteOrderMarkBeforeAFence()
    {
        // With a BOM the first line "- ```" is no fence for the calc module, so "- ```calc"
        // opens its calc block, which runs up to the first ``` line: "v = 7" is a definition
        // there (for Markdown a paragraph after the list).
        Fixture f(u"\ufeff- ```\n- ```calc\n  w = 4\n\nv = 7\n```\n```\n\n| W |\n| --- |\n| =v * 10 + w |\n"_s);
        QCOMPARE(f.cell(f.table(), 0, 0), u"74"_s);
        const int v = f.rowOf(u"v = 7"_s);
        QVERIFY(v > 0);
        f.page.setEditText(v, u"v = 8"_s);
        QCOMPARE(f.cell(f.table(), 0, 0), u"84"_s);
        VERIFY_FRESH(f);
    }

    void languageSwitch()
    {
        Fixture f(Shop);
        f.page.setLocale(u"en"_s);
        QCOMPARE(f.cell(f.table(), 2, 1), u"42.5"_s);
        VERIFY_FRESH(f);
        f.page.setLocale(u"pl"_s);
        QCOMPARE(f.cell(f.table(), 2, 1), u"42,5"_s);
        VERIFY_FRESH(f);
    }

    // --- random edits against a fresh model --------------------------------------------------------

    void resultsGoWithTheView()
    {
        // A block that stops being a calc block or a table in place (undo keeps the block and its
        // row) has no results left.
        Fixture f(u"Akapit źdźbła\n\n- cena:: 12\n"_s);
        f.page.setEditText(0, u"```calc\nx = cena * 2\n```"_s);
        QCOMPARE(f.view(0), u"calc"_s);
        QVERIFY(!f.role(0, BlockListModel::CalcResultsRole).toList().isEmpty());
        f.page.undo();
        QCOMPARE(f.view(0), u"text"_s);
        QCOMPARE(f.role(0, BlockListModel::CalcResultsRole).toList(), QVariantList());
        f.page.setEditText(0, u"| Rzecz | Kwota |\n| --- | ---: |\n| żółw | =cena * 3 |"_s);
        QCOMPARE(f.view(0), u"table"_s);
        QVERIFY(!f.role(0, BlockListModel::TableDataRole).toMap().isEmpty());
        f.page.undo();
        QCOMPARE(f.view(0), u"text"_s);
        QCOMPARE(f.role(0, BlockListModel::TableDataRole).toMap(), QVariantMap());
        QString why;
        QVERIFY2(f.matchesFreshModel(&why), qPrintable(why));
    }

    void randomEditsMatchAFreshModel_data()
    {
        QTest::addColumn<quint32>("seed");
        for (quint32 seed = 1; seed <= 24; ++seed)
            QTest::addRow("seed %u", seed) << seed;
    }

    void randomEditsMatchAFreshModel()
    {
        QFETCH(quint32, seed);
        const QString start = uR"(---
budżet: 1200
---

Wstęp z tekstem ąćęłńóśźż

```calc
cena = 10
ilość = 2
```

- Zakupy
  - stawka:: 25
  - Farba `= cena * ilość`
  - ```calc
    x = cena + 1
    ```
- Notatki
  - Zobacz ((brak)) i `= budżet - x`

| Rzecz | Kwota |
| --- | ---: |
| A | =cena * stawka |
| B | =x |
| Razem | =sum(above) |

- ```calc
  y = 2
z = 3

~~~
```
~~~

Akapit końcowy `= y + z`

| Ostatnia |
| --- |
| =budżet - cena |
)"_s;
        // Texts for text blocks (definitions, fences, frontmatter delimiters, inline calculations
        // and plain prose), for calc blocks and for table cells.
        const QStringList texts{u"zwykły tekst ąćęłńóśźż"_s,
                                u"cena:: 12"_s,
                                u"stawka:: 30"_s,
                                u"x:: 7"_s,
                                u"tekst z :: w środku"_s,
                                u"Wynik `= cena * 2`"_s,
                                u"Zostaje `= budżet - x`"_s,
                                u"---"_s,
                                u"..."_s,
                                u"```calc\nx = 3\n```"_s,
                                u"```calc\ncena = 99"_s,
                                u"~~~"_s,
                                u"```"_s,
                                u"y = 4"_s,
                                u"z = 5"_s,
                                u"zobacz ((brak))"_s,
                                QString(),
                                u"pierwsza linia\ndruga linia"_s,
                                u"# Nagłówek"_s,
                                u"[ ] zadanie"_s,
                                u"| a | b |\n| --- | --- |\n| 1 | =a1 * cena |"_s};
        const QStringList calcTexts{u"cena = 10"_s,  u"cena = 20\nilość = 3"_s, u"x = cena * 2"_s,
                                    u"suma"_s,       u"ilość = 4\n\nsuma"_s,    u"y = cena"_s,
                                    u"zła linia +"_s};
        const QStringList cells{u"5"_s, u"=sum(above)"_s, u"=cena * 2"_s, u"=x + 1"_s, u"tekst"_s, u"=y"_s};

        // Some pages with CRLF line endings, some starting with a BOM (line 0 is then no fence).
        QString content = start;
        if (seed % 3 == 1)
            content.replace(u"\n"_s, u"\r\n"_s);
        if (seed % 4 == 1)
            content.prepend(QChar(0xFEFF));
        if (seed % 6 == 5)
            content.prepend(u"```calc\ncena = 5\n```\n"_s);
        Fixture f(content);
        VERIFY_FRESH(f);
        QRandomGenerator rng(seed);
        const auto pick = [&](const QStringList &list) { return list.at(int(rng.bounded(quint32(list.size())))); };
        for (int step = 0; step < 150; ++step) {
            const int n = f.count();
            if (n == 0) {
                f.page.appendEmptyBlock();
                continue;
            }
            const int row = int(rng.bounded(quint32(n)));
            const QString view = f.view(row);
            const quint32 action = rng.bounded(100u);
            QString what;
            if (action < 30) {
                // Typing: append, insert or replace text.
                QString text = f.edit(row);
                if (view == u"calc")
                    text = rng.bounded(2u) ? pick(calcTexts) : text + u"\nw = 1"_s;
                else if (view == u"table")
                    text = text; // tables are edited by cells
                else if (rng.bounded(3u) == 0)
                    text = pick(texts);
                else
                    text.insert(int(rng.bounded(quint32(text.size() + 1))),
                                pick({u"a"_s, u"ż"_s, u" "_s, u":"_s, u"-"_s, u"`"_s}));
                what = u"setEditText(%1)"_s.arg(row);
                f.page.setEditText(row, text);
            } else if (action < 40 && view == u"table") {
                const QVariantList rows = f.role(row, BlockListModel::TableDataRole).toMap().value(u"rows"_s).toList();
                if (!rows.isEmpty()) {
                    what = u"setTableCell(%1)"_s.arg(row);
                    f.page.setTableCell(row, int(rng.bounded(quint32(rows.size()))), int(rng.bounded(2u)), pick(cells),
                                        rng.bounded(2u) == 0);
                }
            } else if (action < 50) {
                what = u"split(%1)"_s.arg(row);
                f.page.split(row, int(rng.bounded(quint32(f.edit(row).size() + 1))));
            } else if (action < 57) {
                what = u"mergeWithPrevious(%1)"_s.arg(row);
                f.page.mergeWithPrevious(row);
            } else if (action < 64) {
                what = u"insertBlockAfter(%1)"_s.arg(row);
                f.page.insertBlockAfter(row, pick(texts));
            } else if (action < 69) {
                what = u"deleteBlock(%1)"_s.arg(row);
                f.page.deleteBlock(row);
            } else if (action < 74) {
                what = u"indent(%1)"_s.arg(row);
                f.page.indent(row);
            } else if (action < 79) {
                what = u"outdent(%1)"_s.arg(row);
                f.page.outdent(row);
            } else if (action < 84) {
                what = u"move(%1)"_s.arg(row);
                if (rng.bounded(2u))
                    f.page.moveUp(row);
                else
                    f.page.moveDown(row);
            } else if (action < 90) {
                what = u"undo"_s;
                f.page.undo();
            } else if (action < 94) {
                what = u"redo"_s;
                f.page.redo();
            } else if (action < 97) {
                what = u"insertMarkdown(%1)"_s.arg(row);
                f.page.insertMarkdown(row, pick(texts));
            } else {
                what = u"setLocale"_s;
                f.page.setLocale(f.model()->english() ? u"pl"_s : u"en"_s);
            }
            QString why;
            QVERIFY2(f.matchesFreshModel(&why),
                     qPrintable(u"seed %1 step %2 %3: %4"_s.arg(seed).arg(step).arg(what, why)));
        }
    }
};

QTEST_MAIN(TestEditorCalc)

#include "tst_editor_calc.moc"
