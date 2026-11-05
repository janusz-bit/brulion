// The block views' helpers (src/app/editor/blockviews.h): the shaping of BlockListModel's roles
// that their QML did in JavaScript before it was compiled to C++ — each case gives what the
// JavaScript gave (tables: shown rows, next row, column widths, cells; calculation results; page
// properties; list items; suggestion rows; embedded tasks; prepared drawings), plus ChipFlow's
// places (a Flow's, with the "+" never alone on a line) and KeyListModel's kept rows.
#include "blockviews.h"

#include <QAbstractItemModelTester>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QQuickItem>
#include <QSignalSpy>
#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;
using brulion::editor::BlockViews;
using brulion::editor::ChipFlow;
using brulion::editor::KeyListModel;

namespace {

QVariantMap cell(const QString &text, const QString &display, const QString &plain, bool formula = false,
                 const QString &error = {})
{
    return {{u"text"_s, text},   {u"display"_s, display}, {u"html"_s, display.toHtmlEscaped()},
            {u"plain"_s, plain}, {u"error"_s, error},     {u"formula"_s, formula}};
}

/// A table as BlockListModel's `tableData` gives it: 2 columns, 3 rows, the second filtered out.
QVariantMap table()
{
    const QVariantList rows{
            QVariantList{cell(u"Żółw"_s, u"Żółw"_s, u"Żółw"_s), cell(u"12"_s, u"12"_s, u"12"_s)},
            QVariantList{cell(u"**Ąę**"_s, u"**Ąę**"_s, u"Ąę"_s), cell(u"5"_s, u"5"_s, u"5"_s)},
            QVariantList{cell(u"Suma"_s, u"Suma"_s, u"Suma"_s), cell(u"=sum(above)"_s, u"17"_s, u"17"_s, true)}};
    return {{u"header"_s, QStringList{u"Rzecz"_s, u"Cena"_s}},
            {u"headerHtml"_s, QVariantList{u"Rzecz"_s, u"<b>Cena</b>"_s}},
            {u"aligns"_s, QVariantList{QString(), u"right"_s}},
            {u"rows"_s, rows},
            {u"filters"_s, QStringList{QString(), u"> 10"_s}},
            {u"filtering"_s, true},
            {u"shown"_s, QVariantList{true, false, true}},
            {u"shownCount"_s, 2}};
}

} // namespace

class TestEditorBlockViews : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void tableShownRows_data()
    {
        QTest::addColumn<int>("editRow");
        QTest::addColumn<QList<int>>("expected");
        QTest::newRow("none edited") << -2 << QList<int>{0, 2};
        QTest::newRow("the hidden row edited stays") << 1 << QList<int>{0, 1, 2};
        QTest::newRow("a shown row edited") << 2 << QList<int>{0, 2};
        QTest::newRow("the header edited") << -1 << QList<int>{0, 2};
    }
    void tableShownRows()
    {
        QFETCH(int, editRow);
        QFETCH(QList<int>, expected);
        QCOMPARE(BlockViews().tableShownRows(table(), editRow), expected);
    }
    void tableShownRowsWithoutFlags()
    {
        // Rows past the flags (or a table without them) are shown, as `shown[r] !== false`.
        QVariantMap t = table();
        t.insert(u"shown"_s, QVariantList{false});
        QCOMPARE(BlockViews().tableShownRows(t, -2), (QList<int>{1, 2}));
        t.remove(u"shown"_s);
        QCOMPARE(BlockViews().tableShownRows(t, -2), (QList<int>{0, 1, 2}));
        QCOMPARE(BlockViews().tableShownRows({}, -2), QList<int>{});
    }

    void tableNextShownRow_data()
    {
        QTest::addColumn<int>("row");
        QTest::addColumn<int>("step");
        QTest::addColumn<int>("expected");
        QTest::newRow("header to first") << -1 << 1 << 0;
        QTest::newRow("over the hidden row") << 0 << 1 << 2;
        QTest::newRow("past the last") << 2 << 1 << 3;
        QTest::newRow("back over the hidden row") << 2 << -1 << 0;
        QTest::newRow("before the first") << 0 << -1 << -1;
        QTest::newRow("from the hidden row") << 1 << 1 << 2;
    }
    void tableNextShownRow()
    {
        QFETCH(int, row);
        QFETCH(int, step);
        QFETCH(int, expected);
        QCOMPARE(BlockViews().tableNextShownRow(table(), row, step), expected);
    }

    void tableColumnWidths()
    {
        QFont headerFont;
        headerFont.setPixelSize(15);
        headerFont.setWeight(QFont::DemiBold);
        QFont cellFont;
        cellFont.setPixelSize(15);
        const QFontMetricsF header(headerFont);
        const QFontMetricsF cells(cellFont);
        const auto natural = [](qreal w) { return std::min<qreal>(280, std::max<qreal>(56, std::ceil(w) + 2 * 12)); };
        // Column 0: the header and the cells' plain text (not the Markdown: "Ąę", not "**Ąę**").
        const qreal c0 = natural(std::max({header.horizontalAdvance(u"Rzecz"_s), cells.horizontalAdvance(u"Żółw"_s),
                                           cells.horizontalAdvance(u"Ąę"_s), cells.horizontalAdvance(u"Suma"_s)}));
        const qreal c1 = natural(std::max({header.horizontalAdvance(u"Cena"_s), cells.horizontalAdvance(u"12"_s),
                                           cells.horizontalAdvance(u"5"_s), cells.horizontalAdvance(u"17"_s)}));
        const BlockViews views;
        // Wider than the room: the natural widths.
        QCOMPARE(views.tableColumnWidths(table(), headerFont, cellFont, 10, 12), (QList<qreal>{c0, c1}));
        // Narrower: stretched to fill it (floored).
        const qreal width = 2 * (c0 + c1);
        const qreal k = width / (c0 + c1);
        QCOMPARE(views.tableColumnWidths(table(), headerFont, cellFont, width, 12),
                 (QList<qreal>{std::floor(c0 * k), std::floor(c1 * k)}));
        // A long cell is cut at 280, a tiny column grows to 56.
        QVariantMap t = table();
        QVariantList rows = t.value(u"rows"_s).toList();
        rows[0] = QVariantList{cell(QString(200, u'ś'), QString(200, u'ś'), QString(200, u'ś')), cell({}, {}, {})};
        t.insert(u"rows"_s, rows);
        t.insert(u"header"_s, QStringList{u"A"_s, u"B"_s});
        const QList<qreal> clamped = views.tableColumnWidths(t, headerFont, cellFont, 0, 12);
        QCOMPARE(clamped.at(0), 280.0);
        QCOMPARE(clamped.at(1), natural(std::max(header.horizontalAdvance(u"B"_s), cells.horizontalAdvance(u"17"_s))));
        // A cell without `plain` measures its `display`.
        QVariantMap noPlain = cell(u"x"_s, u"wwwwwwwwwwwwwwwwwwww"_s, {});
        noPlain.remove(u"plain"_s);
        rows[0] = QVariantList{noPlain, cell({}, {}, {})};
        t.insert(u"rows"_s, rows);
        QVERIFY(views.tableColumnWidths(t, headerFont, cellFont, 0, 12).at(0)
                >= std::ceil(cells.horizontalAdvance(u"wwwwwwwwwwwwwwwwwwww"_s)));
        QCOMPARE(views.tableColumnWidths({}, headerFont, cellFont, 500, 12), QList<qreal>{});
    }

    void tableCell()
    {
        const BlockViews views;
        const QVariantMap header = views.tableCell(table(), -1, 1);
        QCOMPARE(header.value(u"text"_s).toString(), u"Cena"_s);
        QCOMPARE(header.value(u"display"_s).toString(), u"Cena"_s);
        QCOMPARE(header.value(u"html"_s).toString(), u"<b>Cena</b>"_s);
        QCOMPARE(header.value(u"error"_s).toString(), QString());
        QCOMPARE(header.value(u"formula"_s).toBool(), false);
        const QVariantMap formula = views.tableCell(table(), 2, 1);
        QCOMPARE(formula.value(u"text"_s).toString(), u"=sum(above)"_s);
        QCOMPARE(formula.value(u"display"_s).toString(), u"17"_s);
        QCOMPARE(formula.value(u"formula"_s).toBool(), true);
        // Outside the table: every key, empty.
        for (const auto &[r, c] :
             {std::pair{3, 0}, std::pair{0, 2}, std::pair{-2, 0}, std::pair{0, -1}, std::pair{-1, 5}}) {
            const QVariantMap none = views.tableCell(table(), r, c);
            QCOMPARE(none.size(), 6);
            QCOMPARE(none.value(u"display"_s).toString(), QString());
            QCOMPARE(none.value(u"formula"_s).toBool(), false);
        }
    }

    void calcResults()
    {
        const QVariantList results{
                QVariantMap{{u"line"_s, 0},
                            {u"value"_s, u"12,5 zł"_s},
                            {u"error"_s, QString()},
                            {u"assigned"_s, u"cena"_s}},
                QVariantMap{{u"line"_s, 2}, {u"value"_s, QString()}, {u"error"_s, u"Nieznana zmienna „ż”"_s}},
                QVariantMap{{u"line"_s, 3}, {u"value"_s, QString()}, {u"error"_s, QString()}}};
        QCOMPARE(BlockViews().calcResultTexts(results),
                 (QStringList{u"12,5 zł"_s, u"Nieznana zmienna „ż”"_s, QString()}));
        QCOMPARE(BlockViews().calcResultTexts({}), QStringList{});
    }

    void lineStart_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("line");
        QTest::addColumn<int>("expected");
        QTest::newRow("first") << u"a = 1\nb = 2"_s << 0 << 0;
        QTest::newRow("second") << u"a = 1\nb = 2"_s << 1 << 6;
        QTest::newRow("past the last") << u"a = 1\nb = 2"_s << 2 << 0;
        QTest::newRow("empty lines") << u"\n\nźdźbło"_s << 2 << 2;
        QTest::newRow("after a trailing break") << u"x\n"_s << 1 << 2;
        QTest::newRow("negative") << u"x\ny"_s << -1 << 0;
        QTest::newRow("empty text") << QString() << 1 << 0;
    }
    void lineStart()
    {
        QFETCH(QString, text);
        QFETCH(int, line);
        QFETCH(int, expected);
        QCOMPARE(BlockViews().lineStart(text, line), expected);
    }

    void properties()
    {
        const QVariantList props{QVariantMap{{u"key"_s, u"status"_s},
                                             {u"value"_s, u"w toku"_s},
                                             {u"type"_s, u"text"_s},
                                             {u"raw"_s, u"w toku"_s},
                                             {u"empty"_s, false},
                                             {u"source"_s, u"yaml"_s}},
                                 QVariantMap{{u"key"_s, u"termin"_s},
                                             {u"value"_s, u"2026-10-28"_s},
                                             {u"type"_s, u"date"_s},
                                             {u"raw"_s, u"2026-10-28T09:00"_s},
                                             {u"empty"_s, false}},
                                 QVariantMap{{u"key"_s, u"ilość"_s}, {u"raw"_s, 3.5}, {u"empty"_s, true}}};
        const BlockViews views;
        QCOMPARE(views.propertyOf(props, u"termin"_s).value(u"type"_s).toString(), u"date"_s);
        QVERIFY(views.propertyOf(props, u"Termin"_s).isEmpty());
        QCOMPARE(views.fieldTexts(props, u"key"_s), (QStringList{u"status"_s, u"termin"_s, u"ilość"_s}));
        QCOMPARE(views.fieldTexts(props, u"empty"_s), (QStringList{u"false"_s, u"false"_s, u"true"_s}));
        QCOMPARE(views.fieldTexts(props, u"source"_s), (QStringList{u"yaml"_s, QString(), QString()}));
        QCOMPARE(views.fieldTexts(props, u"raw"_s), (QStringList{u"w toku"_s, u"2026-10-28T09:00"_s, u"3.5"_s}));
        QCOMPARE(views.textAt(props.at(0).toMap(), u"value"_s), u"w toku"_s);
        QCOMPARE(views.textAt(props.at(0).toMap(), u"missing"_s), QString());

        const QVariantMap pageProperties{
                {u"tags"_s, QVariantList{QVariantMap{{u"source"_s, u"yaml"_s}, {u"property"_s, u"tags"_s}}}},
                {u"aliases"_s, QVariantList{QVariantMap{{u"source"_s, u"logseq"_s}, {u"property"_s, u"alias"_s}}}}};
        QCOMPARE(views.headerListKeys(pageProperties), (QStringList{u"yaml:tags"_s, u"logseq:alias"_s}));
        QCOMPARE(views.headerListKeys({}), QStringList{});
    }

    void valueText_data()
    {
        // As JavaScript's String(value) for the types a property's `raw` has.
        QTest::addColumn<QVariant>("value");
        QTest::addColumn<QString>("expected");
        QTest::newRow("text") << QVariant(u"zażółć\ngęślą"_s) << u"zażółć\ngęślą"_s;
        QTest::newRow("true") << QVariant(true) << u"true"_s;
        QTest::newRow("false") << QVariant(false) << u"false"_s;
        QTest::newRow("integer") << QVariant(qlonglong(1200)) << u"1200"_s;
        QTest::newRow("decimal") << QVariant(1.5) << u"1.5"_s;
        QTest::newRow("whole double") << QVariant(3.0) << u"3"_s;
        QTest::newRow("list") << QVariant(QStringList{u"a"_s, u"b c"_s}) << u"a,b c"_s;
        QTest::newRow("invalid") << QVariant() << QString();
    }
    void valueText()
    {
        QFETCH(QVariant, value);
        QFETCH(QString, expected);
        QCOMPARE(BlockViews().valueText(value), expected);
    }

    void splitListItems_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("one") << u"dom"_s << QStringList{u"dom"_s};
        QTest::newRow("commas") << u"a, b ,c"_s << QStringList{u"a"_s, u"b"_s, u"c"_s};
        QTest::newRow("empty parts dropped") << u" , ą,, "_s << QStringList{u"ą"_s};
        QTest::newRow("link with a comma")
                << u"[[Kraków, Polska]], x"_s << QStringList{u"[[Kraków, Polska]]"_s, u"x"_s};
        QTest::newRow("quoted comma") << u"\"a, b\", c"_s << QStringList{u"\"a, b\""_s, u"c"_s};
        QTest::newRow("unclosed link") << u"[[a, b"_s << QStringList{u"[[a, b"_s};
        QTest::newRow("nothing") << QString() << QStringList{};
    }
    void splitListItems()
    {
        QFETCH(QString, text);
        QFETCH(QStringList, expected);
        QCOMPARE(BlockViews().splitListItems(text), expected);
    }

    void listLinkTarget_data()
    {
        QTest::addColumn<QString>("item");
        QTest::addColumn<QString>("expected");
        QTest::newRow("page") << u"[[Łódź]]"_s << u"Łódź"_s;
        QTest::newRow("label") << u"[[Dom|mój dom]]"_s << u"Dom"_s;
        QTest::newRow("spaces") << u"  [[ Dom ]]  "_s << u"Dom"_s;
        QTest::newRow("text") << u"Dom"_s << QString();
        QTest::newRow("two links") << u"[[a]] [[b]]"_s << QString();
        QTest::newRow("empty link") << u"[[]]"_s << QString();
    }
    void listLinkTarget()
    {
        QFETCH(QString, item);
        QFETCH(QString, expected);
        QCOMPARE(BlockViews().listLinkTarget(item), expected);
    }

    void propertyKeyRows()
    {
        const QVariantList keys{QVariantMap{{u"key"_s, u"termin"_s}, {u"count"_s, 3}, {u"type"_s, u"date"_s}},
                                QVariantMap{{u"key"_s, u"mapa"_s}, {u"count"_s, 1}, {u"type"_s, u"unsupported"_s}}};
        const BlockViews views;
        const QVariantList withCount = views.propertyKeyRows(keys, true, false);
        QCOMPARE(withCount.size(), 2);
        const QVariantMap first = withCount.at(0).toMap();
        QCOMPARE(first.value(u"title"_s).toString(), u"termin"_s);
        QCOMPARE(first.value(u"value"_s).toString(), u"termin"_s);
        QCOMPARE(first.value(u"icon"_s).toString(), u"calendar"_s);
        QCOMPARE(first.value(u"subtitle"_s).toString(), u"3 page(s)"_s); // (no translation loaded)
        QCOMPARE(withCount.at(1).toMap().value(u"icon"_s).toString(), u"type"_s);
        const QVariantList plain = views.propertyKeyRows(keys, false, true);
        QCOMPARE(plain.at(0).toMap().value(u"subtitle"_s).toString(), QString());
        QCOMPARE(plain.at(1).toMap().value(u"icon"_s).toString(), u"code"_s);
        for (const QVariant &row : withCount + plain)
            QCOMPARE(row.toMap().keys(), (QStringList{u"icon"_s, u"subtitle"_s, u"title"_s, u"value"_s}));
    }

    void propertyValueRows()
    {
        const QVariantList values{QVariantMap{{u"value"_s, u"[[Dom]]"_s}, {u"count"_s, 0}, {u"kind"_s, u"page"_s}},
                                  QVariantMap{{u"value"_s, u"zakupy"_s}, {u"count"_s, 4}, {u"kind"_s, u"tag"_s}},
                                  QVariantMap{{u"value"_s, u"Żółty"_s}, {u"count"_s, 2}, {u"kind"_s, u"value"_s}},
                                  QVariantMap{{u"value"_s, 7}, {u"count"_s, 0}, {u"kind"_s, u"value"_s}}};
        const BlockViews views;
        // A text value's field: titles as String(value), pages marked.
        const QVariantList text = views.propertyValueRows(values, false, {}, u"PropertyRow"_s);
        QCOMPARE(text.size(), 4);
        QCOMPARE(text.at(0).toMap().value(u"icon"_s).toString(), u"file-text"_s);
        QCOMPARE(text.at(0).toMap().value(u"subtitle"_s).toString(), u"page"_s);
        QCOMPARE(text.at(1).toMap().value(u"title"_s).toString(), u"zakupy"_s);
        QCOMPARE(text.at(1).toMap().value(u"icon"_s).toString(), QString());
        QCOMPARE(text.at(1).toMap().value(u"subtitle"_s).toString(), QString());
        QCOMPARE(text.at(3).toMap().value(u"title"_s).toString(), u"7"_s);
        QCOMPARE(text.at(3).toMap().value(u"value"_s).toString(), u"7"_s);
        // A list's field: tags as #tag with their count, what the list holds left out (any case).
        const QVariantList list = views.propertyValueRows(values, true, {u"żółty"_s}, u"PropertyListValue"_s);
        QCOMPARE(list.size(), 3);
        QCOMPARE(list.at(1).toMap().value(u"title"_s).toString(), u"#zakupy"_s);
        QCOMPARE(list.at(1).toMap().value(u"value"_s).toString(), u"zakupy"_s);
        QCOMPARE(list.at(1).toMap().value(u"icon"_s).toString(), u"hash"_s);
        QCOMPARE(list.at(1).toMap().value(u"subtitle"_s).toString(), u"4"_s);
        QCOMPARE(list.at(2).toMap().value(u"title"_s).toString(), u"7"_s);
        QCOMPARE(list.at(2).toMap().value(u"subtitle"_s).toString(), QString());
    }

    void transclusionTaskLine()
    {
        const auto block = [](const QString &kind, const QString &task, const QString &path, const QString &html,
                              int line) {
            return QVariantMap{
                    {u"kind"_s, kind}, {u"task"_s, task}, {u"path"_s, path}, {u"html"_s, html}, {u"line"_s, line}};
        };
        const QVariantList frames{
                QVariantMap{{u"blocks"_s, QVariantList{block(u"block"_s, u"todo"_s, u"a.md"_s, u"Kupić mąkę"_s, 3),
                                                       block(u"block"_s, QString(), u"a.md"_s, u"Kupić mąkę"_s, 4),
                                                       block(u"embed"_s, u"todo"_s, u"a.md"_s, u"Kupić mąkę"_s, 5)}}},
                QVariantMap{{u"blocks"_s, QVariantList{block(u"block"_s, u"doing"_s, u"a.md"_s, u"Kupić mąkę"_s, 12),
                                                       block(u"block"_s, u"todo"_s, u"b.md"_s, u"Kupić mąkę"_s, 6)}}},
                QVariantMap{{u"title"_s, u"no blocks"_s}}};
        const BlockViews views;
        QCOMPARE(views.transclusionTaskLine(frames, u"a.md"_s, u"Kupić mąkę"_s, 4), 3);
        QCOMPARE(views.transclusionTaskLine(frames, u"a.md"_s, u"Kupić mąkę"_s, 10), 12);
        QCOMPARE(views.transclusionTaskLine(frames, u"b.md"_s, u"Kupić mąkę"_s, 0), 6);
        QCOMPARE(views.transclusionTaskLine(frames, u"a.md"_s, u"Inne"_s, 3), -1);
        QCOMPARE(views.transclusionTaskLine({}, u"a.md"_s, u"Kupić mąkę"_s, 3), -1);
    }

    void preparedItems()
    {
        const QVariantList items{QVariantMap{{u"path"_s, u"a.svg"_s}, {u"error"_s, QString()}},
                                 QVariantMap{{u"path"_s, u"b.svg"_s}, {u"error"_s, u"locked"_s}},
                                 QVariantMap{{u"path"_s, u"c.svg"_s}}};
        const BlockViews views;
        QCOMPARE(views.preparedItems(items), (QVariantList{items.at(0)}));
        QCOMPARE(views.firstItemError(items), u"locked"_s);
        QCOMPARE(views.firstItemError({items.at(0)}), QString());
        // (An item without `error` counts as failed, as `item.error !== ""`.)
        QCOMPARE(views.firstItemError({items.at(0), items.at(2)}), QString());
        QCOMPARE(views.preparedItems({items.at(2)}), QVariantList{});
    }

    void chipFlow()
    {
        QQuickItem root;
        auto *flow = new ChipFlow(&root);
        QQmlParserStatus *status = flow; // (as QML creates it: complete once its children are there)
        status->classBegin();
        flow->setSpacing(4);
        QList<QQuickItem *> chips;
        for (const qreal w : {60, 50, 40}) {
            auto *chip = new QQuickItem(flow);
            chip->setSize(QSizeF(w, 24));
            chips.append(chip);
        }
        auto *add = new QQuickItem(flow);
        add->setSize(QSizeF(28, 28));
        auto *hidden = new QQuickItem(flow);
        hidden->setSize(QSizeF(10, 10));
        hidden->setVisible(false);
        flow->setKeepWith(add);
        flow->setWidth(1000);
        status->componentComplete();
        // One line: side by side, the tallest item the line's height.
        QCOMPARE(chips.at(1)->position(), QPointF(64, 0));
        QCOMPARE(add->position(), QPointF(64 + 54 + 44, 0));
        QCOMPARE(flow->lineHeight(), 28.0);
        QCOMPARE(flow->implicitHeight(), 28.0);
        QCOMPARE(hidden->position(), QPointF(0, 0));
        // "+" would start the second line alone: the chip before it comes down with it.
        QSignalSpy layout(flow, &ChipFlow::layoutChanged);
        flow->setWidth(64 + 54 + 40);
        QCOMPARE(chips.at(2)->position(), QPointF(0, 24 + 4));
        QCOMPARE(add->position(), QPointF(44, 24 + 4));
        QCOMPARE(flow->implicitHeight(), 24.0 + 4 + 28);
        QVERIFY(layout.count() > 0);
        // A chip growing places the others at once (no polish).
        chips.at(0)->setWidth(200);
        QCOMPARE(chips.at(1)->position(), QPointF(0, 24 + 4));
        // layoutFor(): the places at another width, nothing moved.
        const QVariantMap at = flow->layoutFor(1000);
        const QVariantList items = at.value(u"items"_s).toList();
        QCOMPARE(items.size(), 4);
        QCOMPARE(items.at(3).toMap().value(u"item"_s).value<QObject *>(), add);
        QCOMPARE(items.at(3).toMap().value(u"x"_s).toReal(), 204.0 + 54 + 44);
        QCOMPARE(at.value(u"height"_s).toReal(), 28.0);
        QCOMPARE(chips.at(1)->position(), QPointF(0, 24 + 4));
        // A hidden "+" (the field open): the chips alone.
        add->setVisible(false);
        QCOMPARE(add->position(), QPointF(0, 0));
    }

    void keyListModel()
    {
        KeyListModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setKeys({u"tytuł"_s, u"tags"_s, u"termin"_s});
        QCOMPARE(model.count(), 3);
        QCOMPARE(model.data(model.index(1), Qt::UserRole + 1).toString(), u"tags"_s);
        QCOMPARE(model.roleNames().value(Qt::UserRole + 1), QByteArray("key"));
        // Keys that stay keep their rows: moved, never removed and inserted again.
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy moved(&model, &QAbstractItemModel::rowsMoved);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        model.setKeys({u"termin"_s, u"tytuł"_s, u"ilość"_s});
        QCOMPARE(model.keys(), (QStringList{u"termin"_s, u"tytuł"_s, u"ilość"_s}));
        QCOMPARE(removed.count(), 1);
        QCOMPARE(inserted.count(), 1);
        QCOMPARE(moved.count(), 1);
        QCOMPARE(reset.count(), 0);
        model.setKeys({});
        QCOMPARE(model.count(), 0);
        // Repeated keys (a malformed block): as many rows as keys.
        model.setKeys({u"a"_s, u"a"_s, u"b"_s});
        QCOMPARE(model.keys(), (QStringList{u"a"_s, u"a"_s, u"b"_s}));
        model.setKeys({u"b"_s, u"a"_s});
        QCOMPARE(model.keys(), (QStringList{u"b"_s, u"a"_s}));
    }
};

QTEST_MAIN(TestEditorBlockViews)
#include "tst_editor_blockviews.moc"
