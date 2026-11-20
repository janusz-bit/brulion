// Standalone expressions evaluated row by row — the formulas of query views (docs/SYNTAX.md §10
// "Views": Dataview's expression language) and the filters, formulas and summaries of Obsidian
// Bases (§10 "Obsidian Bases": its JavaScript-like language, translated into the same evaluator:
// `file.hasTag("x")`, `price.toFixed(2)`, `list.filter(value > 2)`, `now() - "1 week"`). Part of
// the dataview module — see dataview/dataview.h; nothing from a note runs as a script, and the
// limits of Dataview queries hold (expression depth and nesting, values, steps).
//
//   const auto parsed = dataview::parseExpression(u"round(koszt * 1.23, 2)"_s);
//   dataview::Evaluator eval(index, options);
//   const auto value = eval.evaluate(std::get<dataview::Expression>(parsed), eval.page(path));
//
// Bases syntax: `note.x` / `x` a note property, `file.x` a file property (name — with the
// extension —, basename, path, folder, ext, size, ctime, mtime, tags, links, backlinks, embeds,
// properties) and file methods (hasTag, inFolder, hasLink, hasProperty, asLink), `formula.x` a
// formula (bound by the caller: Evaluator::evaluate's `bound` "formula"), `this` the page showing
// the base, `values` a summary's values; methods of strings, numbers, dates, lists, links, files
// and objects as Obsidian documents them; `==` `!=` `&&` `||` `!`; `date + "1M"`. Regular
// expressions, `html()` as markup and plugin functions are not supported (a clear message).
#pragma once

#include "dataview/dataview.h"
#include "dataview/value.h"

#include <QString>
#include <QStringList>

#include <memory>
#include <variant>

namespace brulion::dataview {

struct Expr; // ast_p.h

/// The language of an expression.
enum class Syntax { Dataview, Bases };

/// A parsed expression: shared, immutable, cheap to copy.
class Expression {
public:
    Expression();
    ~Expression();
    Expression(const Expression &);
    Expression &operator=(const Expression &);

    bool isNull() const { return !m_expr; }
    /// The text it was parsed from.
    const QString &text() const { return m_text; }
    /// Bases: the formulas it reads (`formula.ppu` -> "ppu", canonical keys), in order, each once.
    const QStringList &formulas() const { return m_formulas; }
    /// It is one name and nothing else (`koszt`, `note.status`, `note["Due date"]`): the property
    /// as written ("" when it is more than a name).
    const QString &property() const { return m_property; }

private:
    friend class Evaluator;
    friend std::variant<Expression, QString> parseExpression(const QString &, Syntax);
    std::shared_ptr<const Expr> m_expr;
    QString m_text;
    QStringList m_formulas;
    QString m_property;
};

/// Parse `text` as one expression of `syntax`. The Expression, or a translated message saying what
/// is wrong ("Unknown function “foo()”.", "Regular expressions are not supported.").
std::variant<Expression, QString> parseExpression(const QString &text, Syntax syntax = Syntax::Dataview);

/// Evaluates expressions for rows of one run. Its work counts against Options::maxSteps across
/// all evaluations (a view of many rows stops as one query would), the objects it made are kept
/// for the run.
class Evaluator {
public:
    Evaluator(const VaultIndex &index, Options options);
    ~Evaluator();
    Q_DISABLE_COPY_MOVE(Evaluator)

    /// The row of a note (its page object: fields, `file`), null for a path that is no note.
    Value page(const VaultPath &path);
    /// The row of a block (its list item or task object; its page's fields beneath).
    Value block(const VaultPath &path, int blockIndex);
    /// The row of any file: a note's page object, else an object holding only `file` (name,
    /// path, folder, ext, size, mtime, ctime; empty tags and links) — Bases rows of attachments.
    Value file(const VaultPath &path);
    /// Evaluate `expression` for `row` with `bound` names in scope (latest last: "formula",
    /// "values"). The value, or a translated message (too costly, a value too large).
    std::variant<Value, QString> evaluate(const Expression &expression, const Value &row,
                                          const QList<std::pair<QString, Value>> &bound = {});
    /// Has an evaluation failed for good (the steps ran out)? Every later one fails too.
    bool exhausted() const;

private:
    struct Private;
    std::unique_ptr<Private> d;
};

/// The Bases type name of a value for `isType()`: "null", "boolean", "number", "string", "date",
/// "duration", "list", "object", "link", "file".
QString basesTypeName(const Value &value);

} // namespace brulion::dataview
