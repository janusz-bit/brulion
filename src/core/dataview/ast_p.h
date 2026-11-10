// The parsed form of a Dataview query (parse.cpp builds it, eval.cpp / run.cpp read it).
// Internal to the dataview module.
#pragma once

#include "dataview/dataview.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <memory>

namespace brulion::dataview {

using ExprPtr = std::shared_ptr<const Expr>;

struct Expr {
    enum class Kind {
        Literal,  ///< `value`
        Link,     ///< `[[target]]` written in the query: `name` the target, `text` the display,
                  ///< `raw` the subpath; resolved against the query's page when evaluated
        Variable, ///< `name` (canonical)
        Field,    ///< args[0].name
        Index,    ///< args[0][args[1]]
        Unary,    ///< `name`: "-" or "!"
        Binary,   ///< args[0] name args[1]; name: + - * / % = != < <= > >= and or
        Call,     ///< `name`(args…) — a built-in function, or a variable holding a lambda
        List,     ///< [args…] (written list(…) / array(…) are calls)
        Lambda,   ///< (params) => args[0]
        DateWord, ///< date(today) and friends: `name` the word (today, now, sow, …)
    };
    Kind kind = Kind::Literal;
    Value value;
    QString name;
    QString raw;
    QStringList params;
    QList<ExprPtr> args;
    /// The expression as written (a column's header, FLATTEN's and GROUP BY's default name); for a
    /// chain of plain fields (`file.name`) also what a FLATTEN of it is named. Set on the outermost
    /// node of an operator or field chain only: inner nodes of a chain have none.
    QString text;
    /// Levels from this node down to its deepest leaf (1 for a leaf), at most MaxExpressionDepth:
    /// how deeply evaluating it recurses.
    int depth = 1;
};

struct Source {
    enum class Kind { Tag, Folder, Link, Outgoing, Current, And, Or, Not };
    Kind kind = Kind::Tag;
    QString value; ///< tag (lower-case, no '#'), folder / file path, link target
    std::shared_ptr<const Source> a, b;
    int depth = 1; ///< as Expr::depth
};

struct SortKey {
    ExprPtr expr;
    bool descending = false;
};

struct Command {
    enum class Kind { Where, Sort, GroupBy, Flatten, Limit };
    Kind kind = Kind::Where;
    ExprPtr expr;          ///< Where, GroupBy, Flatten
    QString name;          ///< GroupBy / Flatten: the `AS` name, else the expression's text
    QList<SortKey> sort;   ///< Sort
    int limit = 0;         ///< Limit
    int line = 0;          ///< 1-based line of the command (messages)
};

} // namespace brulion::dataview
