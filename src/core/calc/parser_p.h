// Internal: Pratt parser of calc expressions (tokens -> AST). Not part of the public API.
//
// Precedence, loosest first: statements (`;`, `name = expr`, `f(x) = expr`) · ternary `?:` ·
// or · xor · and · not · comparisons (chained: 1 < x <= 3) · conversion `to`/`in`/`na`/`w` ·
// `x as % of y` · `+ -` · `* / mod %(modulo)` and `20% of x` · implicit multiplication
// (`5 km`, `2 pi`, `2(3)`) · unary `- + !` · `^ **` (right-assoc.) · postfix `! %` and
// superscripts (`m²`) · `√`.
#pragma once

#include "calc/ast_p.h"
#include "calc/errors.h"
#include "calc/lexer_p.h"

#include <QList>
#include <QStringList>

#include <climits>
#include <vector>

namespace brulion::calc {

/// A table reference found in a formula (tables only).
struct TableRef {
    enum class Kind { Cell, Range, Header, Direction };
    enum class Direction { Above, Below, Left, Right };

    Kind kind = Kind::Cell;
    int row1 = 0; ///< 0-based data row (row 1 = first row under the header)
    int col1 = 0;
    int row2 = 0; ///< INT_MAX for whole-column ranges ("B:B")
    int col2 = 0;
    Direction direction = Direction::Above;
    QString text; ///< as written ("A1", "A1:B3", "[Cena]", "above")
};

/// One statement of a line (statements are separated by ';').
struct Statement {
    enum class Kind { Expression, Assignment, FunctionDefinition };
    Kind kind = Kind::Expression;
    QString name;       ///< assigned variable / defined function
    QStringList params; ///< function parameters
    NodePtr expr;
};

struct ParseOptions {
    /// Allow `name = expr`, `f(x) = expr` and several statements separated by ';'. When off
    /// (table formulas) a single expression is parsed and `=` compares.
    bool statements = true;
    /// Recognise table references: A1, A1:B3, A:C, [Header], above/below/left/right.
    bool tableRefs = false;
    /// Normalised column headers (see normalizeHeader) for [Header] references.
    QStringList headers;
};

struct ParsedProgram {
    std::vector<Statement> statements;
    QList<TableRef> refs;
    CalcError error;
};

/// Parse the tokens of one line. `source` is the lexed text (for error messages and headers).
ParsedProgram parseProgram(const QList<Token> &tokens, QStringView source, const ParseOptions &options);

/// Header text as matched by [Header] references: inline markup stripped, NFC, whitespace
/// collapsed, lower-case.
QString normalizeHeader(QStringView header);

/// 0-based column index of spreadsheet letters ("A" -> 0, "AA" -> 26).
int columnIndexOf(QStringView letters);

} // namespace brulion::calc
