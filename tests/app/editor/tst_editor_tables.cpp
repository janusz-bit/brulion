// Tables and charts in the editor (PageController, BlockListModel, ChartView): sorting a table (one
// undo step, the rest of the page byte for byte, CRLF), the view's filters (nothing written, rows
// shown), CSV out (clipboard, file with a BOM) and in ("/Tabela z CSV…"), the chartData of a ```chart
// block following its table, and ChartView laying it out and painting it in both themes.
// BRULION_CHART_SHOTS=<dir> saves the painted charts as PNG.
#include "blocklistmodel.h"
#include "chartview.h"
#include "fakeeditorhost.h"
#include "pagecontroller.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

using namespace brulion;
using namespace brulion::editor;
using namespace Qt::StringLiterals;

namespace {

const QString Page = u"Tabele.md"_s;

const QString Content = u"# Zakupy\n\n| Pozycja | Ilość | Cena |\n|:--|--:|--:|\n| Żółta farba | 2 | 40 |\n"
                        u"| Wałek \\| mały | 3 | 12,50 |\n| Ćwieki | 100 | 0,10 |\n| Łata | 1 | 89 |\n"
                        u"| Suma | | =sum(above) |\n\nKoniec strony.\n"_s;

struct Fixture {
    FakeEditorHost host;
    PageController page;

    explicit Fixture(const QString &content)
    {
        host.setFile(Page, content);
        page.setNow(u"2026-11-03 10:00"_s);
        page.setSaveDelay(0);
        page.setHost(&host);
        page.setPath(Page);
    }
    BlockListModel *model() const { return page.blocks(); }
    QVariant role(int row, const char *name) const
    {
        const QHash<int, QByteArray> roles = model()->roleNames();
        return model()->data(model()->index(row), roles.key(name));
    }
    int rowOf(const QString &view) const
    {
        for (int r = 0; r < model()->rowCount(); ++r) {
            if (role(r, "view").toString() == view)
                return r;
        }
        return -1;
    }
    QStringList firstColumn(int row) const
    {
        QStringList out;
        for (const QVariant &r : role(row, "tableData").toMap().value(u"rows"_s).toList())
            out.append(r.toList().value(0).toMap().value(u"text"_s).toString());
        return out;
    }
};

QImage paintChart(ChartView &v, qreal width, bool dark)
{
    v.setWidth(width);
    v.setHeight(v.implicitHeight());
    v.setDark(dark);
    v.setTextColor(QColor(dark ? u"#e8e4da"_s : u"#1f2430"_s));
    v.setMutedColor(QColor(dark ? u"#a8a395"_s : u"#535a6a"_s));
    v.setBackgroundColor(QColor(dark ? u"#1c2130"_s : u"#fbf7ee"_s));
    v.setBorderColor(QColor(dark ? u"#3a3f4d"_s : u"#ddd6c6"_s));
    QImage img(QSize(int(width), std::max(1, int(v.height()))), QImage::Format_ARGB32_Premultiplied);
    img.fill(dark ? QColor(u"#1c2130"_s) : QColor(u"#fbf7ee"_s));
    QPainter p(&img);
    v.paint(&p);
    p.end();
    return img;
}

/// How many pixels differ from the background.
int inkOf(const QImage &img)
{
    const QRgb bg = img.pixel(0, 0);
    int n = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            n += img.pixel(x, y) != bg ? 1 : 0;
    return n;
}

} // namespace

class TestEditorTables : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void sort()
    {
        Fixture f(Content);
        const int row = f.rowOf(u"table"_s);
        QVERIFY(row >= 0);
        QVERIFY(f.page.sortTable(row, 0, false));
        QCOMPARE(f.firstColumn(row),
                 (QStringList{u"Ćwieki"_s, u"Łata"_s, u"Wałek | mały"_s, u"Żółta farba"_s, u"Suma"_s}));
        // The rows keep their lines (the escaped pipe too), the rest of the page is as it was.
        QCOMPARE(f.page.text(), u"# Zakupy\n\n| Pozycja | Ilość | Cena |\n|:--|--:|--:|\n| Ćwieki | 100 | 0,10 |\n"
                                u"| Łata | 1 | 89 |\n| Wałek \\| mały | 3 | 12,50 |\n| Żółta farba | 2 | 40 |\n"
                                u"| Suma | | =sum(above) |\n\nKoniec strony.\n"_s);
        // Numbers by value, descending.
        QVERIFY(f.page.sortTable(row, 2, true));
        QCOMPARE(f.firstColumn(row),
                 (QStringList{u"Łata"_s, u"Żółta farba"_s, u"Wałek | mały"_s, u"Ćwieki"_s, u"Suma"_s}));
        QVERIFY(!f.page.sortTable(row, 2, true)); // in that order already
        QVERIFY(!f.page.sortTable(row, 7, false));
        QVERIFY(!f.page.sortTable(0, 0, false)); // the heading is no table
        // Each sort one undo step; back byte for byte.
        f.page.undo();
        f.page.undo();
        QCOMPARE(f.page.text(), Content);
    }

    void sortCrlf()
    {
        QString crlf = Content;
        crlf.replace(u"\n"_s, u"\r\n"_s);
        Fixture f(crlf);
        const int row = f.rowOf(u"table"_s);
        QVERIFY(f.page.sortTable(row, 1, false));
        QVERIFY(f.page.text().contains(
                u"| Łata | 1 | 89 |\r\n| Żółta farba | 2 | 40 |\r\n| Wałek \\| mały | 3 | 12,50 |"_s));
        QVERIFY(!f.page.text().contains(QRegularExpression(u"[^\r]\n"_s)));
        f.page.undo();
        QCOMPARE(f.page.text(), crlf);
    }

    void filters()
    {
        Fixture f(Content);
        const int row = f.rowOf(u"table"_s);
        const auto table = [&] { return f.role(row, "tableData").toMap(); };
        QCOMPARE(table().value(u"filtering"_s).toBool(), false);
        QCOMPARE(table().value(u"shownCount"_s).toInt(), 5);
        f.page.setTableFilterOpen(row, true);
        QCOMPARE(table().value(u"filtering"_s).toBool(), true);
        f.page.setTableFilter(row, 2, u"> 20"_s);
        QCOMPARE(table().value(u"shown"_s).toList(),
                 (QVariantList{true, false, false, true, true})); // the summary row stays
        QCOMPARE(table().value(u"shownCount"_s).toInt(), 3);
        QCOMPARE(table().value(u"filters"_s).toStringList(), (QStringList{QString(), QString(), u"> 20"_s}));
        f.page.setTableFilter(row, 0, u"zolta"_s);
        QCOMPARE(table().value(u"shownCount"_s).toInt(), 2);
        // Nothing written, nothing to undo.
        QCOMPARE(f.page.text(), Content);
        QVERIFY(!f.page.canUndo());
        // The filters last through edits of the table; closing the row clears them.
        f.page.setTableCell(row, 2, 2, u"25"_s);
        QCOMPARE(table().value(u"shownCount"_s).toInt(), 2);
        f.page.setTableFilterOpen(row, false);
        QCOMPARE(table().value(u"shownCount"_s).toInt(), 5);
        QCOMPARE(table().value(u"filters"_s).toStringList(), (QStringList{QString(), QString(), QString()}));
        // Out of range: nothing.
        f.page.setTableFilter(-1, 0, u"x"_s);
        f.page.setTableFilter(row, -1, u"x"_s);
    }

    void csvOut()
    {
        Fixture f(Content);
        const int row = f.rowOf(u"table"_s);
        const QString csv = f.page.tableCsv(row);
        QVERIFY2(csv.startsWith(u"Pozycja,Ilość,Cena\r\nŻółta farba,2,40\r\nWałek | mały,3,\"12,50\"\r\n"_s),
                 qPrintable(csv));
        QVERIFY(csv.contains(u"Suma,,\"141,"_s)); // the formula's result (40 + 12,5 + 0,1 + 89)
        QVERIFY(f.page.tableCsv(0).isEmpty());
        QVERIFY(f.page.copyTableCsv(row));
        QCOMPARE(QGuiApplication::clipboard()->text(), csv);
        QCOMPARE(f.page.tableCsvFileName(row), u"Tabele.csv"_s);
        QTemporaryDir dir;
        const QUrl url = QUrl::fromLocalFile(dir.filePath(u"tabela.csv"_s));
        QVERIFY(f.page.saveTableCsv(row, url));
        QFile file(url.toLocalFile());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray data = file.readAll();
        QVERIFY(data.startsWith("\xEF\xBB\xBF")); // spreadsheets read UTF-8 with it
        QCOMPARE(QString::fromUtf8(data.mid(3)), csv);
        QVERIFY(!f.page.saveTableCsv(0, url));
        QVERIFY(!f.page.saveTableCsv(row, QUrl::fromLocalFile(dir.filePath(u"nie/ma/takiego.csv"_s))));
        QVERIFY(!f.page.downloadTableCsv(row)); // (the browser only)
    }

    void csvIn()
    {
        Fixture f(u"# Import\n\n- \n"_s);
        const int empty = 1;
        const QVariantMap r =
                f.page.insertCsvTable(empty, u"\uFEFFMiasto;Ludność\r\nŁódź;\"655 279\"\r\nGdańsk;486 345\r\n"_s);
        QVERIFY(r.value(u"row"_s).toInt() >= 0);
        QCOMPARE(r.value(u"truncated"_s).toBool(), false);
        const int row = f.rowOf(u"table"_s);
        QVERIFY(row >= 0);
        QCOMPARE(f.firstColumn(row), (QStringList{u"Łódź"_s, u"Gdańsk"_s}));
        // One undo step back.
        f.page.undo();
        QCOMPARE(f.page.text(), u"# Import\n\n- \n"_s);
        // A file, read as UTF-8 or windows-1250.
        QTemporaryDir dir;
        QFile file(dir.filePath(u"dane.csv"_s));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Imi\xea,Miasto\nJ\xf3zef,\xa3\xf3"
                   "d\x9f\n\xaf"
                   "aneta,Gda\xf1sk\n"); // windows-1250: ę ó Ł ź Ż ń
        file.close();
        QVERIFY(f.page.insertCsvFile(empty, QUrl::fromLocalFile(file.fileName())).value(u"row"_s).toInt() >= 0);
        QVERIFY2(f.page.text().contains(u"| Imię   | Miasto |"_s), qPrintable(f.page.text()));
        QVERIFY(f.page.text().contains(u"| Józef  | Łódź   |"_s));
        QVERIFY(f.page.text().contains(u"| Żaneta | Gdańsk |"_s));
        // Nothing to read: nothing inserted.
        const QString before = f.page.text();
        QCOMPARE(f.page.insertCsvTable(empty, u"\n\n"_s).value(u"row"_s).toInt(), -1);
        QCOMPARE(f.page.insertCsvFile(empty, QUrl::fromLocalFile(dir.filePath(u"brak.csv"_s))).value(u"row"_s).toInt(),
                 -1);
        QCOMPARE(f.page.text(), before);
        QVERIFY(!f.page.pickCsvInBrowser(empty));
    }

    void chartData()
    {
        Fixture f(Content + u"\n```chart\ntype: bar\nx: Pozycja\ny: Cena\ntitle: Ceny\n```\n"_s);
        const int row = f.rowOf(u"chart"_s);
        QVERIFY(row >= 0);
        const auto data = [&] { return f.role(row, "chartData").toMap(); };
        QVERIFY2(data().value(u"ok"_s).toBool(), qPrintable(data().value(u"error"_s).toString()));
        QCOMPARE(data().value(u"labels"_s).toStringList(),
                 (QStringList{u"Żółta farba"_s, u"Wałek | mały"_s, u"Ćwieki"_s, u"Łata"_s}));
        const QVariantList values = data().value(u"series"_s).toList().value(0).toMap().value(u"values"_s).toList();
        QCOMPARE(values.size(), 4);
        const QList<double> expected{40, 12.5, 0.1, 89};
        for (int i = 0; i < 4; ++i)
            QVERIFY(qFuzzyCompare(values.at(i).toDouble(), expected.at(i)));
        // The chart follows its table.
        const int table = f.rowOf(u"table"_s);
        QSignalSpy changed(f.model(), &QAbstractItemModel::dataChanged);
        f.page.setTableCell(table, 0, 2, u"=2*21"_s);
        QCOMPARE(data().value(u"series"_s).toList().value(0).toMap().value(u"values"_s).toList().value(0),
                 QVariant(42.0));
        // A mistake: the translated message with its line.
        f.page.setTableCell(table, 0, 2, u"40"_s);
        Fixture bad(Content + u"\n```chart\ntype: radar\n```\n"_s);
        const QVariantMap err = bad.role(bad.rowOf(u"chart"_s), "chartData").toMap();
        QCOMPARE(err.value(u"ok"_s).toBool(), false);
        QVERIFY2(err.value(u"error"_s).toString().contains(u"radar"_s), qPrintable(err.value(u"error"_s).toString()));
        // A query chart asks the host.
        Fixture query(
                u"- [ ] jedno #dom\n- [x] drugie #dom\n\n```chart\nquery: type:tasks group:status\ntype: pie\n```\n"_s);
        const QVariantMap q = query.role(query.rowOf(u"chart"_s), "chartData").toMap();
        QVERIFY2(q.value(u"ok"_s).toBool(), qPrintable(q.value(u"error"_s).toString()));
        QCOMPARE(q.value(u"labels"_s).toStringList().size(), 2);
    }

    void chartView()
    {
        Fixture f(Content + u"\n```chart\ntype: line\ny: Cena, Ilość\ntitle: Ceny i ilości\n```\n"_s);
        const int row = f.rowOf(u"chart"_s);
        ChartView v;
        QSignalSpy result(&v, &ChartView::resultChanged);
        v.setSource(f.role(row, "editText").toString());
        v.setChartData(f.role(row, "chartData").toMap());
        QCOMPARE(v.status(), u"ok"_s);
        QVERIFY(v.message().isEmpty());
        QVERIFY2(v.description().startsWith(u"Line chart “Ceny i ilości”: Żółta farba: Cena 40, Ilość 2"_s),
                 qPrintable(v.description()));
        QVERIFY(result.count() > 0);
        // The height follows the width; nothing is laid out wider than asked.
        v.setWidth(700);
        const qreal tall = v.implicitHeight();
        QVERIFY(tall > 100);
        v.setWidth(300);
        QVERIFY(v.implicitHeight() < tall);
        QCOMPARE(v.scene().size.width(), 300.0);
        // Painted in both themes.
        const QString dir = qEnvironmentVariable("BRULION_CHART_SHOTS");
        for (const QString &type : {u"bar"_s, u"line"_s, u"pie"_s}) {
            v.setSource(u"type: "_s + type + u"\ny: Cena, Ilość\ntitle: Ceny i ilości"_s);
            QCOMPARE(v.status(), u"ok"_s);
            for (const bool dark : {false, true}) {
                const QImage img = paintChart(v, 640, dark);
                QVERIFY2(inkOf(img) > 2000, qPrintable(type));
                if (!dir.isEmpty())
                    img.save(QDir(dir).filePath(u"chart-"_s + type + (dark ? u"-dark.png"_s : u".png"_s)));
            }
        }
        // Errors: the model's message; a spec mistake its own.
        v.setChartData({{u"ok"_s, false}, {u"error"_s, u"Brak tabeli."_s}});
        QCOMPARE(v.status(), u"error"_s);
        QCOMPARE(v.message(), u"Brak tabeli."_s);
        QCOMPARE(v.implicitHeight(), 0.0);
        v.setSource(u"kolor: czerwony"_s);
        QVERIFY(v.message().contains(u"kolor"_s));
        // Hostile data maps: labels without values, values that are no numbers.
        v.setSource(u"type: bar"_s);
        v.setChartData(
                {{u"ok"_s, true},
                 {u"labels"_s, QStringList{u"a"_s, u"b"_s}},
                 {u"series"_s, QVariantList{QVariantMap{{u"name"_s, u"x"_s}, {u"values"_s, QVariantList{u"x"_s}}}}}});
        QCOMPARE(v.status(), u"ok"_s);
        QVERIFY(paintChart(v, 400, false).width() == 400);
    }
};

QTEST_MAIN(TestEditorTables)
#include "tst_editor_tables.moc"
