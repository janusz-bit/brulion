// Charts in notes (docs/SYNTAX.md §9 "Charts"): a ```chart block names its data — a table of the
// page (with formulas: their results), a ```calc block or a query's results — and how to draw it
// (bar, line or pie; which columns). Brulion's own code reads and draws it: no script engine, no
// web view, nothing from a note is executed. Notes come from sync and imports: every input is
// bounded (limits below), values that are no finite numbers are gaps, labels are cut, and a
// malformed chart gives an Error with its line, never a crash.
//
//   ```chart
//   type: bar              // bar | line | pie   (typ: słupkowy | liniowy | kołowy)
//   table: above           // the table above (default) | 2 = the page's second table
//   x: Miesiąc             // the labels' column (default: the first)
//   y: Przychód, Koszt     // the values' columns (default: the columns of numbers)
//   title: Budżet 2026
//   ```
//
// parseSpec() reads the block; resolveData() takes the numbers from the page (PageSources: the
// tables and calc blocks of the page, a query runner when there is one); layoutChart() lays them
// out as a diagram::Scene (paths and text in theme roles: the app paints it with MermaidView's
// painter, the HTML export writes it as SVG with diagram::toSvg()); describeChart() is the text
// alternative (screen readers, the export). Messages are translated (context "ChartMessages").
//
// QtCore only.
#pragma once

#include "calc/calclocale.h"
#include "calc/value.h"
#include "diagram/diagram.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <functional>
#include <optional>

namespace brulion::chart {

namespace limits {
inline constexpr qsizetype MaxSpecLength = 10 * 1024; ///< characters of the block
inline constexpr int MaxPoints = 400;                 ///< labels (table rows, calc lines, groups)
inline constexpr int MaxSeries = 12;                  ///< value columns
inline constexpr int MaxSlices = 64;                  ///< pie slices
inline constexpr qsizetype MaxLabelLength = 200;      ///< characters of a label (longer: cut, "…")
inline constexpr double MaxMagnitude = 1e15;          ///< larger values are gaps
inline constexpr qreal MinWidth = 200;                ///< the width a chart is laid out at, clamped
inline constexpr qreal MaxWidth = 4000;
} // namespace limits

enum class Type { Bar, Line, Pie };
enum class SourceKind { Table, Calc, Query };

/// Why a chart cannot be drawn. message() says it in the UI language.
struct Error {
    enum class Code {
        Syntax,           ///< a line that is no `key: value` (`detail`: its text, cut)
        UnknownKey,       ///< `detail`: the key
        InvalidType,      ///< `detail`: the value of `type:`
        InvalidSource,    ///< `table:` / `calc:` with something that names none (`detail`)
        SeveralSources,   ///< more than one of table / calc / query
        NoTable,          ///< no such table on the page (`detail`: "above" or its number)
        NoCalc,           ///< no such calc block on the page
        UnknownColumn,    ///< `detail`: the name
        NoValues,         ///< no column of numbers / no number at all
        NoPositiveValues, ///< a pie without a value above zero
        TooLarge,         ///< `detail`: "points", "series", "slices", "source"
        QueryFailed,      ///< `detail`: the query's own (translated) message
        QueryUnavailable, ///< no query runner here (the export of a single file)
        EmptyQuery,       ///< `query:` without a query
    };
    Code code = Code::UnknownKey;
    int line = 0; ///< 1-based line in the block (0: the whole chart)
    QString detail;

    /// "Line 3: …" in the UI language (QCoreApplication's translator).
    QString message() const;
};

/// A ```chart block read.
struct Spec {
    Type type = Type::Bar;
    SourceKind source = SourceKind::Table;
    /// Table / Calc: 0 = the last one above the chart, n >= 1 = the page's n-th.
    int sourceIndex = 0;
    QString query; ///< Query: the query (the text of a ```query block, on one line)
    QString x;     ///< the labels' column ("" = the first)
    QStringList y; ///< the values' columns ("" = every column of numbers)
    QString title;
    int xLine = 0; ///< lines of `x:` / `y:` / the source (errors point at them)
    int yLine = 0;
    int sourceLine = 0;
};

struct SpecResult {
    Spec spec;
    std::optional<Error> error;
};

/// Read the block's text (between the fences): `key: value` lines; keys case- and
/// diacritic-insensitive with Polish names; `//` comments and `#` lines; blank lines skipped.
SpecResult parseSpec(QStringView source);

/// The numbers of a chart: one label per point, one value per series and point (NaN: a gap).
struct Series {
    QString name;
    QList<double> values;
    friend bool operator==(const Series &, const Series &) = default;
};
struct Data {
    QStringList labels;
    QList<Series> series;
    QString labelsName; ///< the labels' column ("" when none)
    friend bool operator==(const Data &, const Data &) = default;
};

/// What a query gives a chart: a label and a number per group (or the query's error).
struct QueryCounts {
    QList<std::pair<QString, double>> groups;
    QString error; ///< translated; set: the query failed
};

/// The data sources of a page. Lines are 0-based lines of the page.
struct PageSources {
    struct Table {
        int line = 0;
        QString text; ///< the table's Markdown (calc::parseTable() reads it)
    };
    struct Calc {
        int line = 0;
        QString source; ///< the block's lines between the fences
    };
    QList<Table> tables; ///< in page order
    QList<Calc> calcs;   ///< in page order
    int chartLine = 0;   ///< the chart block's first line ("above" = before it)
    calc::CalcLocale locale = calc::CalcLocale::Polish;
    /// The calc variables a table at `line` sees (calc::pageScope()); unset: those of the calc
    /// blocks above it.
    std::function<calc::Scope(int line)> scopeAt;
    /// Runs a query; unset: Error::QueryUnavailable.
    std::function<QueryCounts(const QString &query)> runQuery;
};

/// The sources of `content` (a whole page): its tables and calc blocks (calc::findTables,
/// calc::findCalcBlocks) with calc::pageScope() for formulas. `chartLine`: the chart's line.
PageSources pageSources(QStringView content, int chartLine, calc::CalcLocale locale = calc::CalcLocale::Polish);

struct DataResult {
    Data data;
    std::optional<Error> error;
};

/// The chart's numbers from the page. Tables: the rows except summary rows (calc/tablesort.h), the
/// labels from `x` (cells as shown, light Markdown left out), the values of each `y` column
/// (a formula's result, else the cell read as a number; anything else a gap); without `y`, every
/// column (other than `x`) whose cells are mostly numbers. Calc: each line with a numeric result
/// (assignments and `Label:` lines by their name, others by their text; `sum` lines left out).
/// Query: one point per group (QueryCounts).
DataResult resolveData(const Spec &spec, const PageSources &page);

struct LayoutOptions {
    qreal width = 640; ///< clamped to limits::MinWidth..MaxWidth
    diagram::Metrics metrics = diagram::approximateMetrics(13);
    calc::CalcLocale locale = calc::CalcLocale::Polish; ///< the axis' numbers
};

/// The chart drawn into a scene `options.width` wide (its height follows). Colours are roles:
/// series s fills with diagram::sliceRole(s) and strokes with diagram::seriesRole(s).
diagram::Scene layoutChart(const Spec &spec, const Data &data, const LayoutOptions &options = {});

/// The text alternative: the type, title and every value ("Wykres słupkowy „Budżet”: Styczeń –
/// Przychód 1200, Koszt 900; …"), cut after about `maxLength` characters.
QString describeChart(const Spec &spec, const Data &data, calc::CalcLocale locale = calc::CalcLocale::Polish,
                      qsizetype maxLength = 2000);

/// The chart's type in words ("Bar chart"), translated.
QString typeName(Type type);

/// parseSpec() + resolveData() + layoutChart() in one: the scene, or the error.
struct Result {
    Spec spec;
    Data data;
    diagram::Scene scene;
    std::optional<Error> error;
    bool ok() const { return !error.has_value(); }
};
Result render(QStringView source, const PageSources &page, const LayoutOptions &options = {});

} // namespace brulion::chart
