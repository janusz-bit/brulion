// Calculations in Brulion notes (docs/SYNTAX.md §9): ```calc blocks, inline `= expr` code and
// GFM table formulas. Brulion's own expression engine — tokenizer, Pratt parser, evaluator — no
// script engine, no eval. Never throws and never crashes on hostile input: every problem is a
// per-line error string; input length (4000 characters per expression), nesting, recursion,
// list size (20 000 items), string size (100 000 characters) and total work (also of `sum` lines,
// string copies and list building) are bounded. Results are plain text, never HTML.
// Stack: realistic calculations need well under 512 KiB; the deepest hostile input allowed by
// the limits needs about 1 MiB, so call from the GUI thread or a thread with >= 2 MiB of stack.
//
// API overview (namespace brulion::calc; locale = CalcLocale::Polish | English, Polish default)
//
//   Calc blocks and inline calculations                                         calc/calc.h
//     evaluateCalc(source, Scope &scope, locale) -> QList<CalcLineResult>    (scope is updated)
//         CalcLineResult{line, input, value, error, assigned, numeric, label}
//     evaluateInline(expr, const Scope &, locale) -> CalcLineResult         ("= cena * 2")
//     pageScope(content, uptoLine = -1, locale) -> Scope     variables of the calc blocks that
//         end before `uptoLine` + numeric page properties (frontmatter, `key:: value`); cached
//     PageCalc(content, locale).scopeAt(line)          the same without the global cache
//     findCalcBlocks(content) -> QList<CalcBlock>      ```calc / ~~~calc fences (also in lists)
//
//   Values                                                                      calc/value.h
//     Value (number in SI units + Unit + currency tag + percent flag | bool | string | list |
//         user function), Value::format(locale), Value::numeric()
//     Scope: set/value/contains/number/formatted/names/ans — variables by NFC name
//
//   Tables                                                                      calc/table.h
//     parseTable(md) -> optional<TableModel>{header, aligns, rows}; serializeTable(model)
//     findTables(content) -> QList<TableSpan>{startLine, endLine, from, to, indent}
//     replaceTable(content, span, model) -> content with the table rewritten (EOLs preserved)
//     evaluateTable(model, locale, scope) -> TableEval{values, errors}   ("#ERR", "#CYCLE")
//     displayWidth(text), splitTableRow(line), parseDelimiterRow(line), columnName/cellName
//
//   Numbers, units, errors                       calc/numbers.h, calc/units.h, calc/errors.h
//     formatNumber(1234.5, Polish) == "1234,5", formatNumber(12345.5, Polish) == "12 345,5"
//     (no-break space), English "12,345.5"; formatMoney; parseLocaleNumber("1 234,50 zł")
//     findUnit("km"), Unit; CalcError{code, arg}.message(locale)
//
// Calc language (one expression or `;`-separated statements per line):
//   numbers   12.5 · 12,5 (decimal comma outside function arguments, or inside calls using ';';
//             in calls without ';' when only that fits the function: sqrt(2,25) = 1.5, pow(2,5)
//             = 32, and round(2,5) / max(1,5) are errors, never a guess)
//             · 1 000 000 · 1e3 · English 1,000.5 · 15% · ∞ π e
//   operators + - * / ^ ** × ÷ − · mod and % between operands (10 % 3) · n! · √x · x² ·
//             comparisons == != <> < <= > >= = ≤ ≥ ≠ (chained) · and or xor not && || ! · c ? a : b
//             · juxtaposition multiplies (5 km, 2 pi, 2(3), 0,5 [Cena]) and binds tighter than
//             * and /, except in `number / number factor` (mathjs "rule 2", as in v1): 1/2 kg is
//             half a kilogram, 100 km / 2 h is 50 km/h; lines of thousands of terms are fine
//   strings   "…" or '…' (a ' right after a word is an apostrophe: "Jan's share: 50")
//   percent   x + 10% (= x·1,1) · x - 10% · 20% of x / 20% z x / 20% od x · x as % of y /
//             x jako % z y · percent variables keep the meaning (rabat = 15%; cena - rabat)
//   units     5 km, 3 h + 20 min, 100 km / 2 h -> 50 km/h, 5 km to mi / in / na / w mi,
//             20°C to °F (affine only in conversions); see calc/units.h for the list
//   money     12 zł, 5 PLN, €10, $5, £3, 10 €, CHF — tags that survive arithmetic; different
//             currencies combine into a plain number; conversion between them is an error
//   functions sum/suma avg/average/mean/średnia min max count/ile median/mediana
//             round/zaokr(x[, n]) floor ceil trunc abs sign sqrt cbrt pow exp ln log(x[, base])
//             (log = log10) log10 log2 sin cos tan asin acos atan atan2 mod concat factorial
//             if/jeżeli/jeśli(cond, a[, b]) — case-insensitive; ',' or ';' between arguments
//   blocks    `name = expr` (Unicode names: ilość), `f(x) = x^2`, `ans`, `# comment`,
//             `// comment`, `Label: expr`, bare `sum`/`total`/`suma`/`razem` and
//             `avg`/`średnia` = aggregate of the results above up to the previous blank line
#pragma once

#include "calc/calclocale.h"
#include "calc/errors.h"
#include "calc/numbers.h"
#include "calc/table.h"
#include "calc/units.h"
#include "calc/value.h"

#include <QList>
#include <QString>
#include <QStringView>

#include <optional>

namespace brulion::calc {

/// Result of one line of a calc block (or of an inline calculation).
struct CalcLineResult {
    int line = 0;                  ///< 0-based line within the block source
    QString input;                 ///< the line as written
    QString value;                 ///< formatted result; empty for blank lines, comments, errors
    QString error;                 ///< short, translated error message; empty when none
    QString assigned;              ///< variable/function assigned on this line ("ilość")
    std::optional<double> numeric; ///< magnitude of a numeric result in its displayed unit
    QString label;                 ///< text before ':' ("Czynsz: 1800" -> "Czynsz")

    bool hasValue() const { return !value.isEmpty(); }
    bool hasError() const { return !error.isEmpty(); }
};

/// Evaluate the source of a ```calc block line by line (any line endings; one result per line).
/// Lines share `scope`, which is updated with assignments and `ans`. Results of a group of lines
/// (up to the previous blank line) can be aggregated with `sum`/`total`/`suma`/`razem` and
/// `avg`/`average`/`mean`/`średnia`; percentages, aggregate lines and errors don't join the
/// group, percentages don't replace `ans`. Never throws.
QList<CalcLineResult> evaluateCalc(QStringView source, Scope &scope, CalcLocale locale = CalcLocale::Polish);

/// Evaluate an inline calculation: the code of `` `= cena * 2` `` (a leading '=' is optional).
/// `scope` is not modified. An empty expression gives an empty result (no value, no error).
CalcLineResult evaluateInline(QStringView expression, const Scope &scope, CalcLocale locale = CalcLocale::Polish);

/// A fenced calc block in a page.
struct CalcBlock {
    int startLine = 0;        ///< 0-based line of the opening fence
    int endLine = 0;          ///< line of the closing fence (last line when unclosed)
    int contentStartLine = 0; ///< first content line (startLine + 1)
    QString source;           ///< content with the fence indentation removed, joined by '\n'
    bool closed = true;

    friend bool operator==(const CalcBlock &a, const CalcBlock &b)
    {
        return a.startLine == b.startLine && a.endLine == b.endLine && a.contentStartLine == b.contentStartLine
               && a.source == b.source && a.closed == b.closed;
    }
};

/// Locate ```calc and ~~~calc blocks (info string case-insensitive; fences may be indented or
/// start a list item, Logseq style). Other fences are skipped.
QList<CalcBlock> findCalcBlocks(QStringView content);

/// The calc variables of a page, evaluated once and queryable for any line: numeric frontmatter
/// values, numeric `key:: value` properties (keys become identifiers: due-date -> due_date;
/// "Cena" is also available as "cena") and every calc block, in document order.
class PageCalc {
public:
    explicit PageCalc(QStringView content, CalcLocale locale = CalcLocale::Polish);

    /// Variables defined by lines before `uptoLine` (0-based): properties above it and calc
    /// blocks whose closing fence is above it. `uptoLine < 0`: the whole page, including a
    /// calc block left unclosed at the end.
    Scope scopeAt(int uptoLine = -1) const;

private:
    struct Change {
        QString name;
        Value value;
    };
    struct Event {
        int line = 0;
        QList<Change> changes;
    };
    QList<Event> m_events;
};

/// PageCalc(content, locale).scopeAt(uptoLine) with a small thread-safe cache of recently
/// evaluated pages, so many inline calculations on one page stay cheap. Never throws.
Scope pageScope(QStringView content, int uptoLine = -1, CalcLocale locale = CalcLocale::Polish);

} // namespace brulion::calc
