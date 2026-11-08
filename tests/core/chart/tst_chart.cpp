// Charts (chart/chart.h): the ```chart syntax, the data from tables, calc blocks and queries, hostile
// input and the layout's bounds; the scene as SVG is well-formed and holds nothing active.
#include "chart/chart.h"
#include "diagram/svg.h"

#include <QElapsedTimer>
#include <QTest>
#include <QXmlStreamReader>

#include <cmath>
#include <limits>

using namespace brulion;
using namespace brulion::chart;
using namespace Qt::StringLiterals;

namespace {

using Code = Error::Code;

bool wellFormed(const QString &svg, QStringList *elements = nullptr)
{
    QXmlStreamReader xml(svg);
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && elements)
            elements->append(xml.name().toString());
    }
    return !xml.hasError();
}

/// A page: `before`, then the chart block `chart` (its first line is the fence), then `after`.
Result renderOn(const QString &before, const QString &chart, const QString &after = {})
{
    const QString page = before + u"```chart\n"_s + chart + u"\n```\n"_s + after;
    const int line = int(before.count(u'\n'));
    return render(chart, pageSources(page, line));
}

const QString kBudget = u"# Budżet\n\n| Miesiąc | Przychód | Koszt | Uwagi |\n|---|--:|--:|---|\n"
                        u"| Styczeń | 1 200 zł | 900 | zimno |\n| Luty | 1 500 zł | =B2-300 | |\n"
                        u"| Marzec | 900 zł | 950 | **źle** |\n| Razem | =sum(above) | =sum(above) | |\n\n"_s;

QList<double> values(const Result &r, int series = 0)
{
    return r.data.series.value(series).values;
}

} // namespace

class TestChart : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void spec_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<int>("type");
        QTest::addColumn<int>("sourceKind");
        QTest::addColumn<int>("index");
        QTest::addColumn<QString>("x");
        QTest::addColumn<QStringList>("y");
        QTest::addColumn<QString>("title");
        QTest::newRow("empty: defaults") << QString() << int(Type::Bar) << int(SourceKind::Table) << 0 << QString()
                                         << QStringList() << QString();
        QTest::newRow("english") << u"type: line\ntable: 2\nx: Month\ny: Income, Cost\ntitle: Budget 2026"_s
                                 << int(Type::Line) << int(SourceKind::Table) << 2 << u"Month"_s
                                 << QStringList{u"Income"_s, u"Cost"_s} << u"Budget 2026"_s;
        QTest::newRow("polish") << u"Typ: Kołowy\nTabela: powyżej\nEtykiety: Miesiąc\nWartości: Przychód\nTytuł: Żółć"_s
                                << int(Type::Pie) << int(SourceKind::Table) << 0 << u"Miesiąc"_s
                                << QStringList{u"Przychód"_s} << u"Żółć"_s;
        QTest::newRow("no diacritics") << u"typ: slupkowy\nwartosci: Koszt"_s << int(Type::Bar)
                                       << int(SourceKind::Table) << 0 << QString() << QStringList{u"Koszt"_s}
                                       << QString();
        QTest::newRow("comments and blanks")
                << u"// mój wykres\n\n# też komentarz\ntype: line   // linie\n  y: A ; \"B, C\" ; [D]\n"_s
                << int(Type::Line) << int(SourceKind::Table) << 0 << QString() << QStringList{u"A"_s, u"B, C"_s, u"D"_s}
                << QString();
        QTest::newRow("calc") << u"type: pie\ncalc: above"_s << int(Type::Pie) << int(SourceKind::Calc) << 0
                              << QString() << QStringList() << QString();
        QTest::newRow("calc numbered") << u"obliczenia: 3"_s << int(Type::Bar) << int(SourceKind::Calc) << 3
                                       << QString() << QStringList() << QString();
        QTest::newRow("query") << u"query: type:tasks group:status // zadania"_s << int(Type::Bar)
                               << int(SourceKind::Query) << 0 << QString() << QStringList() << QString();
        QTest::newRow("title with a url") << u"title: http://example.com"_s << int(Type::Bar) << int(SourceKind::Table)
                                          << 0 << QString() << QStringList() << u"http://example.com"_s;
    }
    void spec()
    {
        QFETCH(QString, source);
        QFETCH(int, type);
        QFETCH(int, sourceKind);
        QFETCH(int, index);
        QFETCH(QString, x);
        QFETCH(QStringList, y);
        QFETCH(QString, title);
        const SpecResult r = parseSpec(source);
        QVERIFY2(!r.error, qPrintable(r.error ? r.error->message() : QString()));
        QCOMPARE(int(r.spec.type), type);
        QCOMPARE(int(r.spec.source), sourceKind);
        QCOMPARE(r.spec.sourceIndex, index);
        QCOMPARE(r.spec.x, x);
        QCOMPARE(r.spec.y, y);
        QCOMPARE(r.spec.title, title);
        if (sourceKind == int(SourceKind::Query))
            QCOMPARE(r.spec.query, u"type:tasks group:status"_s);
    }

    void specErrors_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<int>("code");
        QTest::addColumn<int>("line");
        QTest::newRow("no colon") << u"type: bar\nhello"_s << int(Code::Syntax) << 2;
        QTest::newRow("unknown key") << u"\ncolour: red"_s << int(Code::UnknownKey) << 2;
        QTest::newRow("unknown type") << u"type: radar"_s << int(Code::InvalidType) << 1;
        QTest::newRow("bad table") << u"table: ostatnia"_s << int(Code::InvalidSource) << 1;
        QTest::newRow("table zero") << u"table: 0"_s << int(Code::InvalidSource) << 1;
        QTest::newRow("negative") << u"calc: -1"_s << int(Code::InvalidSource) << 1;
        QTest::newRow("two sources") << u"table: 1\ncalc: 1"_s << int(Code::SeveralSources) << 2;
        QTest::newRow("empty query") << u"query:   "_s << int(Code::EmptyQuery) << 1;
        QTest::newRow("colon first") << u": bar"_s << int(Code::Syntax) << 1;
        QTest::newRow("too long") << QString(limits::MaxSpecLength + 1, u'a') << int(Code::TooLarge) << 0;
    }
    void specErrors()
    {
        QFETCH(QString, source);
        QFETCH(int, code);
        QFETCH(int, line);
        const SpecResult r = parseSpec(source);
        QVERIFY(r.error);
        QCOMPARE(int(r.error->code), code);
        QCOMPARE(r.error->line, line);
        QVERIFY(!r.error->message().isEmpty());
        if (line > 0)
            QVERIFY2(r.error->message().contains(QString::number(line)), qPrintable(r.error->message()));
    }

    void fromTable()
    {
        // Defaults: the labels from the first column, every column of numbers, the summary row out.
        const Result r = renderOn(kBudget, QString());
        QVERIFY2(r.ok(), qPrintable(r.error ? r.error->message() : QString()));
        QCOMPARE(r.data.labels, (QStringList{u"Styczeń"_s, u"Luty"_s, u"Marzec"_s}));
        QCOMPARE(r.data.labelsName, u"Miesiąc"_s);
        QCOMPARE(r.data.series.size(), 2);
        QCOMPARE(r.data.series.at(0).name, u"Przychód"_s);
        QCOMPARE(values(r, 0), (QList<double>{1200, 1500, 900}));
        QCOMPARE(values(r, 1), (QList<double>{900, 1200, 950})); // a formula's result
        QVERIFY(r.scene.size.width() > 0 && r.scene.size.height() > 0);
        QVERIFY(!r.scene.items.isEmpty());
    }

    void labelsAsTheyShow()
    {
        // Labels and series names read as the table shows them, as "Copy as plain text" does:
        // emphasis inside the text, links (their labels), code spans and line breaks; tags stay.
        const QString table = u"| **Pozycja** | Cena *netto* | `Ilość` |\n|---|--:|--:|\n"
                              u"| **Płytki** z Castorama | 120 | 3 |\n"
                              u"| Klej *elastyczny* | 45 | 2 |\n"
                              u"| [[Fuga szara]] #zakupy | 30 | 1 |\n"
                              u"| `Silikon` i [taśma](https://example.com/tasma) | 20 | 4 |\n"
                              u"| Listwy<br>__przypodłogowe__ | 18 | 40 |\n\n"_s;
        const Result r = renderOn(table, QString());
        QVERIFY2(r.ok(), qPrintable(r.error ? r.error->message() : QString()));
        QCOMPARE(r.data.labels, (QStringList{u"Płytki z Castorama"_s, u"Klej elastyczny"_s, u"Fuga szara #zakupy"_s,
                                             u"Silikon i taśma"_s, u"Listwy przypodłogowe"_s}));
        QCOMPARE(r.data.labelsName, u"Pozycja"_s);
        QCOMPARE(r.data.series.size(), 2);
        QCOMPARE(r.data.series.at(0).name, u"Cena netto"_s);
        QCOMPARE(r.data.series.at(1).name, u"Ilość"_s);
        QCOMPARE(values(r, 0), (QList<double>{120, 45, 30, 20, 18}));
        const QString text = describeChart(r.spec, r.data);
        QVERIFY2(text.contains(u"Płytki z Castorama: Cena netto 120"_s) && !text.contains(u'*') && !text.contains(u'`')
                         && !text.contains(u"https"_s),
                 qPrintable(text));
        // A formula's label is its result.
        const Result f =
                renderOn(u"| Tydzień | Wynik |\n|---|--:|\n| =40+5 | 5 |\n| **46** | 7 |\n\n"_s, u"x: Tydzień"_s);
        QVERIFY2(f.ok(), qPrintable(f.error ? f.error->message() : QString()));
        QCOMPARE(f.data.labels, (QStringList{u"45"_s, u"46"_s}));
    }

    void columns_data()
    {
        QTest::addColumn<QString>("chart");
        QTest::addColumn<QStringList>("labels");
        QTest::addColumn<QList<double>>("first");
        QTest::addColumn<int>("series");
        QTest::newRow("by name, any case")
                << u"x: miesiąc\ny: KOSZT"_s << QStringList{u"Styczeń"_s, u"Luty"_s, u"Marzec"_s}
                << QList<double>{900, 1200, 950} << 1;
        QTest::newRow("without diacritics")
                << u"x: miesiac\ny: przychod"_s << QStringList{u"Styczeń"_s, u"Luty"_s, u"Marzec"_s}
                << QList<double>{1200, 1500, 900} << 1;
        QTest::newRow("by letter") << u"x: D\ny: C, B"_s << QStringList{u"zimno"_s, QString(), u"źle"_s}
                                   << QList<double>{900, 1200, 950} << 2;
        QTest::newRow("the same twice") << u"y: Koszt, koszt"_s << QStringList{u"Styczeń"_s, u"Luty"_s, u"Marzec"_s}
                                        << QList<double>{900, 1200, 950} << 1;
    }
    void columns()
    {
        QFETCH(QString, chart);
        QFETCH(QStringList, labels);
        QFETCH(QList<double>, first);
        QFETCH(int, series);
        const Result r = renderOn(kBudget, chart);
        QVERIFY2(r.ok(), qPrintable(r.error ? r.error->message() : QString()));
        QCOMPARE(r.data.labels, labels);
        QCOMPARE(values(r, 0), first);
        QCOMPARE(r.data.series.size(), series);
    }

    void dataErrors_data()
    {
        QTest::addColumn<QString>("before");
        QTest::addColumn<QString>("chart");
        QTest::addColumn<int>("code");
        QTest::newRow("no table above") << QString() << QString() << int(Code::NoTable);
        QTest::newRow("table below only") << QString() << u"table: above"_s << int(Code::NoTable);
        QTest::newRow("no such table") << kBudget << u"table: 2"_s << int(Code::NoTable);
        QTest::newRow("no calc") << kBudget << u"calc: above"_s << int(Code::NoCalc);
        QTest::newRow("unknown x") << kBudget << u"x: Rok"_s << int(Code::UnknownColumn);
        QTest::newRow("unknown y") << kBudget << u"y: Przychód, Zysk"_s << int(Code::UnknownColumn);
        QTest::newRow("no numbers") << u"| a | b |\n|---|---|\n| x | y |\n\n"_s << QString() << int(Code::NoValues);
        QTest::newRow("text column") << kBudget << u"y: Uwagi"_s << int(Code::NoValues);
        QTest::newRow("pie of negatives") << u"| a | b |\n|---|---|\n| x | -1 |\n| y | 0 |\n\n"_s << u"type: pie"_s
                                          << int(Code::NoPositiveValues);
        QTest::newRow("query here") << QString() << u"query: type:tasks"_s << int(Code::QueryUnavailable);
    }
    void dataErrors()
    {
        QFETCH(QString, before);
        QFETCH(QString, chart);
        QFETCH(int, code);
        const QString after = QTest::currentDataTag() == QByteArray("table below only") ? kBudget : QString();
        const Result r = renderOn(before, chart, after);
        QVERIFY(!r.ok());
        QCOMPARE(int(r.error->code), code);
        QVERIFY(r.scene.items.isEmpty());
    }

    void numberedTable()
    {
        // `table: 1` is the page's first table, also one below the chart.
        const Result r = renderOn(QString(), u"table: 1\ntype: line"_s, kBudget);
        QVERIFY2(r.ok(), qPrintable(r.error ? r.error->message() : QString()));
        QCOMPARE(r.data.labels.size(), 3);
    }

    void formulasSeeCalcVariables()
    {
        const QString before = u"```calc\nstawka = 100\n```\n\n| Dzień | Godziny | Kwota |\n|---|---|---|\n"
                               u"| pn | 2 | =[Godziny]*stawka |\n| wt | 3 | =[Godziny]*stawka |\n\n"_s;
        const Result r = renderOn(before, u"y: Kwota"_s);
        QVERIFY2(r.ok(), qPrintable(r.error ? r.error->message() : QString()));
        QCOMPARE(values(r), (QList<double>{200, 300}));
        // Without the page's scope: the calc blocks above (PageSources by hand, as the export does).
        PageSources page;
        page.chartLine = 100;
        page.calcs.append({0, u"stawka = 10"_s});
        page.tables.append({4, u"| Dzień | Godziny | Kwota |\n|---|---|---|\n| pn | 2 | =[Godziny]*stawka |"_s});
        const Result e = render(u"y: Kwota"_s, page);
        QVERIFY(e.ok());
        QCOMPARE(values(e), (QList<double>{20}));
    }

    void fromCalc()
    {
        const QString before = u"```calc\n# wydatki\nczynsz = 1800\nJedzenie: 1 200\n300 + 50   // inne\n\nsuma\n"
                               u"błąd = 1/0\ntekst = \"abc\"\n```\n"_s;
        const Result r = renderOn(before, u"calc: above\ntype: pie"_s);
        QVERIFY2(r.ok(), qPrintable(r.error ? r.error->message() : QString()));
        QCOMPARE(r.data.labels, (QStringList{u"czynsz"_s, u"Jedzenie"_s, u"300 + 50"_s}));
        QCOMPARE(values(r), (QList<double>{1800, 1200, 350}));
    }

    void fromQuery()
    {
        PageSources page;
        QString asked;
        page.runQuery = [&](const QString &q) {
            asked = q;
            QueryCounts c;
            c.groups = {
                    {u"Do zrobienia"_s, 3}, {u"W toku"_s, 1}, {u"Zrobione"_s, std::numeric_limits<double>::infinity()}};
            return c;
        };
        const Result r = render(u"query: type:tasks group:status\ntype: pie"_s, page);
        QVERIFY(r.ok());
        QCOMPARE(asked, u"type:tasks group:status"_s);
        QCOMPARE(r.data.labels.size(), 3);
        QVERIFY(std::isnan(values(r).at(2))); // infinity: a gap
        page.runQuery = [](const QString &) { return QueryCounts{{}, u"Nieznany klucz „x”"_s}; };
        const Result failed = render(u"query: x:y"_s, page);
        QVERIFY(!failed.ok());
        QCOMPARE(failed.error->code, Code::QueryFailed);
        QVERIFY(failed.error->message().contains(u"Nieznany klucz"_s));
        page.runQuery = [](const QString &) { return QueryCounts{}; };
        QCOMPARE(render(u"query: type:tasks"_s, page).error->code, Code::NoValues);
    }

    void hostileValues()
    {
        // NaN, infinities, huge numbers and text are gaps; the chart still draws the rest.
        const QString before = u"| a | b |\n|---|---|\n| x | nan |\n| y | inf |\n| z | 1e300 |\n| w | 5 |\n"
                               u"| v | =1/0 |\n| u | dużo |\n\n"_s;
        for (const QString &type : {u"bar"_s, u"line"_s, u"pie"_s}) {
            const Result r = renderOn(before, u"y: b\ntype: "_s + type);
            QVERIFY2(r.ok(), qPrintable(type));
            const QList<double> v = values(r);
            QCOMPARE(v.size(), 6);
            QCOMPARE(std::count_if(v.cbegin(), v.cend(), [](double d) { return std::isnan(d); }), 5);
            QVERIFY(std::isfinite(r.scene.size.width()) && std::isfinite(r.scene.size.height()));
            for (const diagram::Item &it : r.scene.items) {
                for (const diagram::PathOp &op : it.path)
                    QVERIFY(std::isfinite(op.p.x()) && std::isfinite(op.p.y()));
            }
        }
    }

    void hugeTable()
    {
        QString big = u"| a | b |\n|---|---|\n"_s;
        for (int i = 0; i <= limits::MaxPoints; ++i)
            big += u"| r"_s + QString::number(i) + u" | "_s + QString::number(i) + u" |\n"_s;
        QElapsedTimer timer;
        timer.start();
        const Result r = renderOn(big + u'\n', QString());
        QVERIFY(!r.ok());
        QCOMPARE(r.error->code, Code::TooLarge);
        // At the bound: drawn, in bounded time, labels thinned out.
        QString atBound = u"| a | b |\n|---|---|\n"_s;
        for (int i = 0; i < limits::MaxPoints; ++i)
            atBound += u"| Wiersz z długą etykietą "_s + QString::number(i) + u" | "_s + QString::number(i % 17 - 8)
                       + u" |\n"_s;
        for (const QString &type : {u"bar"_s, u"line"_s}) {
            const Result ok = renderOn(atBound + u'\n', u"type: "_s + type);
            QVERIFY(ok.ok());
            int texts = 0;
            for (const diagram::Item &it : ok.scene.items)
                texts += it.type == diagram::Item::Type::Text ? 1 : 0;
            QVERIFY2(texts < 100, qPrintable(QString::number(texts)));
        }
        QVERIFY2(timer.elapsed() < 20000, qPrintable(QString::number(timer.elapsed())));
        // Too many series asked for.
        QString wide = u"|"_s;
        QString delimiter = u"|"_s;
        QString row = u"|"_s;
        QStringList names;
        for (int c = 0; c <= limits::MaxSeries + 1; ++c) {
            wide += u" k"_s + QString::number(c) + u" |"_s;
            delimiter += u"---|"_s;
            row += u" 1 |"_s;
            if (c > 0)
                names.append(u"k"_s + QString::number(c));
        }
        const Result series =
                renderOn(wide + u'\n' + delimiter + u'\n' + row + u"\n\n"_s, u"y: "_s + names.join(u", "_s));
        QCOMPARE(series.error->code, Code::TooLarge);
        // Without `y`: the first MaxSeries columns of numbers.
        const Result capped = renderOn(wide + u'\n' + delimiter + u'\n' + row + u"\n\n"_s, QString());
        QVERIFY(capped.ok());
        QCOMPARE(capped.data.series.size(), limits::MaxSeries);
    }

    void longLabels()
    {
        const QString label(5000, u'ż');
        const QString before = u"| a | b |\n|---|---|\n| "_s + label + u" | 3 |\n| krótka | 4 |\n\n"_s;
        const Result r = renderOn(before, u"title: "_s + QString(300, u'T') + u"\ny: b"_s);
        QVERIFY(r.ok());
        QCOMPARE(r.data.labels.at(0).size(), limits::MaxLabelLength);
        LayoutOptions narrow;
        narrow.width = 1; // clamped
        for (const Type type : {Type::Bar, Type::Line, Type::Pie}) {
            Spec spec = r.spec;
            spec.type = type;
            const diagram::Scene s = layoutChart(spec, r.data, narrow);
            QCOMPARE(s.size.width(), limits::MinWidth);
            for (const diagram::Item &it : s.items) {
                if (it.type == diagram::Item::Type::Text)
                    QVERIFY2(it.lines.value(0).size() < 200, qPrintable(QString::number(it.lines.value(0).size())));
            }
        }
        LayoutOptions huge;
        huge.width = std::numeric_limits<qreal>::infinity();
        QCOMPARE(layoutChart(r.spec, r.data, huge).size.width(), 640.0);
        huge.width = 1e9;
        QCOMPARE(layoutChart(r.spec, r.data, huge).size.width(), limits::MaxWidth);
    }

    void layouts()
    {
        for (const QString &type : {u"bar"_s, u"line"_s, u"pie"_s}) {
            const Result r = renderOn(kBudget, u"title: Budżet & <koszty>\ntype: "_s + type);
            QVERIFY(r.ok());
            QCOMPARE(r.scene.title, u"Budżet & <koszty>"_s);
            // Every item inside the scene.
            const QRectF bounds(QPointF(-1, -1), r.scene.size + QSizeF(2, 2));
            for (const diagram::Item &it : r.scene.items) {
                for (const diagram::PathOp &op : it.path)
                    QVERIFY2(bounds.contains(op.p), qPrintable(type));
            }
            const diagram::Palette dark = diagram::darkPalette();
            QStringList elements;
            const QString svg = diagram::toSvg(r.scene, diagram::lightPalette(), &dark);
            QVERIFY(wellFormed(svg, &elements));
            QVERIFY(!svg.contains(u"<koszty>"_s));
            for (const QString &e : std::as_const(elements))
                QVERIFY2((QStringList{u"svg"_s, u"title"_s, u"style"_s, u"path"_s, u"rect"_s, u"text"_s}.contains(e)),
                         qPrintable(e));
            // Deterministic.
            QCOMPARE(diagram::toSvg(renderOn(kBudget, u"title: Budżet & <koszty>\ntype: "_s + type).scene,
                                    diagram::lightPalette(), &dark),
                     svg);
        }
        // Two series: a legend with both names.
        const Result two = renderOn(kBudget, QString());
        QStringList texts;
        for (const diagram::Item &it : two.scene.items)
            texts += it.lines;
        QVERIFY(texts.contains(u"Przychód"_s) && texts.contains(u"Koszt"_s));
        QVERIFY(texts.contains(u"Styczeń"_s));
    }

    void palettes()
    {
        // The roles a chart paints in have colours in both themes.
        for (const diagram::Palette &p : {diagram::lightPalette(), diagram::darkPalette()}) {
            for (int s = 0; s < 14; ++s)
                QVERIFY(!p[diagram::seriesRole(s)].isEmpty());
            QVERIFY(!p[diagram::Role::Grid].isEmpty());
        }
        QVERIFY(diagram::lightPalette()[diagram::seriesRole(0)] != diagram::darkPalette()[diagram::seriesRole(0)]);
    }

    void describe()
    {
        const Result r = renderOn(kBudget, u"title: Budżet"_s);
        const QString text = describeChart(r.spec, r.data);
        QVERIFY2(text.startsWith(u"Bar chart “Budżet”: Styczeń: Przychód 1"_s), qPrintable(text));
        QVERIFY(text.contains(u"Marzec"_s));
        const Result pie = renderOn(kBudget, u"type: pie\ny: Koszt"_s);
        const QString pieText = describeChart(pie.spec, pie.data);
        QVERIFY2(pieText.contains(u"Luty: 1"_s) && pieText.contains(u"%)"_s), qPrintable(pieText));
        // Bounded.
        QString big = u"| a | b |\n|---|---|\n"_s;
        for (int i = 0; i < 300; ++i)
            big += u"| etykieta "_s + QString::number(i) + u" | "_s + QString::number(i) + u" |\n"_s;
        const Result many = renderOn(big + u'\n', QString());
        QVERIFY(many.ok());
        QVERIFY(describeChart(many.spec, many.data, calc::CalcLocale::Polish, 500).size() <= 500);
    }
};

QTEST_GUILESS_MAIN(TestChart)
#include "tst_chart.moc"
