// Evaluation of Dataview expressions (eval.cpp), the built-in functions (functions.cpp) and the
// page, file and task objects rows are made of (objects.cpp). Internal to the dataview module.
#pragma once

#include "dataview/ast_p.h"
#include "dataview/dataview.h"

#include <QHash>
#include <QScopeGuard>
#include <QString>

#include <memory>
#include <optional>

namespace brulion::dataview {

/// Thrown when an evaluation must stop (too costly, too deep, too many rows, a value too large):
/// caught by runQuery().
struct EvalError {
    QString message;
};

/// Throws the EvalError of a value past MaxValueItems or MaxTextLength (value.h).
[[noreturn]] void throwTooLarge();
/// Throws that EvalError when a value would hold more than MaxValueItems values or MaxTextLength
/// characters. Value::list() and the like check every value they make; what can grow a value many
/// times over in one step (`+`, split(), join(), toText(), a field of a list) checks before building it.
inline void checkSize(qint64 items, qint64 chars = 0)
{
    if (items > MaxValueItems || chars > MaxTextLength)
        throwTooLarge();
}

/// The values a builder keeps while it works — map()'s results, the keys of sort(), SORT and
/// GROUP BY — counted as they come like the parts of one value: past the limits an EvalError
/// before their memory is spent.
class Kept {
public:
    void add(const Value &value)
    {
        m_extent.add(value.extent());
        checkSize(m_extent.items, m_extent.chars);
    }

private:
    Extent m_extent{1, 1, 0};
};

/// Names and their values, the latest last: lambda parameters, FLATTEN and GROUP BY names.
using Bound = QList<std::pair<QString, Value>>;

/// What every evaluation of one run shares: the index, the options, the objects made so far.
class Context {
public:
    Context(const VaultIndex &index, const Options &options);

    const VaultIndex &index() const { return m_index; }
    const Options &options() const { return m_options; }

    /// One step of work; throws EvalError past Options::maxSteps.
    void step(qint64 n = 1)
    {
        m_steps += n;
        if (m_steps > m_options.maxSteps)
            throwTooCostly();
    }
    /// Characters of text copied, built or read for one step (a list costs a step per value): a
    /// query copying a long text or a large list again and again runs out of steps, not of time.
    static constexpr qsizetype CharsPerStep = 64;
    void stepText(qsizetype chars) { step(chars / CharsPerStep); }
    /// A list or a text an expression builds, paid for in steps (a value each, CharsPerStep
    /// characters each).
    Value list(ValueList items)
    {
        step(items.size());
        return Value::list(std::move(items));
    }
    Value text(const QString &s)
    {
        stepText(s.size());
        return Value::string(s);
    }
    /// Starts a call of a lambda whose body is `levels` deep (Expr::depth), which ends with the
    /// returned guard. The bodies being evaluated inside one another nest at most MaxCallDepth
    /// levels in all (a lambda applied to itself never stops): past it this throws EvalError
    /// instead of overflowing the stack — evaluating recurses once per level.
    [[nodiscard]] auto enterCall(int levels)
    {
        if (m_callDepth + levels > MaxCallDepth)
            throwTooDeep();
        m_callDepth += levels;
        return qScopeGuard([this, levels] { m_callDepth -= levels; });
    }

    /// The page object of a note (made once per run); null Value for a path that is no note.
    Value page(const VaultPath &path);
    /// `this`: the query's page.
    Value thisPage();
    /// A link written in a note of `from` (or in the query: the query's page).
    LinkValue resolveLink(const QString &target, const QString &display, const QString &subpath, bool embed,
                          const VaultPath &from) const;
    /// The shortest [[target]] of a page (VaultIndex::linkTarget()).
    QString linkTarget(const VaultPath &path) const;
    /// A tag's spelling as first written in the vault ("Praca" for #praca).
    QString tagDisplay(const QString &tag);
    /// The task objects of a note's blocks (TASK rows, `file.tasks`): every block that is a task.
    ValueList tasksOf(const VaultPath &path);
    /// The list items of a note (`file.lists`): every list block, tasks included.
    ValueList listsOf(const VaultPath &path);
    /// The object of block `blockIndex` of `path` (a list item or task).
    Value item(const VaultPath &path, int blockIndex);

private:
    static constexpr int MaxCallDepth = 64;
    [[noreturn]] static void throwTooCostly();
    [[noreturn]] static void throwTooDeep(); // eval.cpp

    const VaultIndex &m_index;
    const Options &m_options;
    qint64 m_steps = 0;
    int m_callDepth = 0;
    QHash<VaultPath, Value> m_pages;
    QHash<QString, Value> m_items; ///< "path\x1fblock"
    std::optional<QHash<QString, QString>> m_tagDisplay;
};

/// A row while the query runs: the page or task it came from, the object its fields are read
/// from, and the fields FLATTEN and GROUP BY added on top.
struct RowData {
    VaultPath path;
    int blockIndex = -1;
    Value base;  ///< page / task object, or a group object
    Bound extra; ///< names set by FLATTEN / GROUP BY (latest last)
    bool group = false;
    Value key;           ///< group rows: the key
    QList<RowData> rows; ///< group rows: the rows grouped
};

/// The row as an object: its `extra` names first, then the base object.
Value rowObject(const RowData &row);

/// A lambda: parameters, body and the names it was written among.
struct Function {
    Function(QStringList paramNames, ExprPtr bodyExpr, Value rowValue, Bound boundNames); // eval.cpp

    QStringList params;
    ExprPtr body;
    Value row;     ///< the row it was written in
    Bound bound;   ///< parameters of enclosing lambdas
    int depth = 1; ///< Extent::depth: one more than the deepest of the row and the bound values
};

/// Evaluate `expr` for a row object (`row`; null outside rows) with `bound` names in scope.
Value evaluate(const Expr &expr, Context &ctx, const Value &row, const Bound &bound = {});
/// Call a function value with arguments.
Value callFunction(const Function &f, const ValueList &args, Context &ctx);
/// A field of a value: an object's field, a link's page field, a date's or duration's part, a
/// list's elements' fields (flattened one level). Null when missing.
Value fieldOf(const Value &value, const QString &key, Context &ctx);

/// A built-in function: its arity and its body (its canonical name is its key in the table).
struct Builtin {
    int minArgs = 0;
    int maxArgs = 0; ///< -1: any number
    Value (*call)(const ValueList &args, Context &ctx) = nullptr;
};
/// The built-in `name` (lower-case), or nullptr.
const Builtin *findBuiltin(const QString &name);
/// A Dataview function Brulion does not provide (regular expressions, csv, …).
bool isUnsupportedFunction(const QString &name);

/// Text of a value for string concatenation and `string()`: Markdown for links ("[[Dom]]"),
/// dates ISO, numbers without trailing zeros; "…" for a list or object `depth` levels inside the
/// value shown, MaxValueDepth or more (a task object's children go as deep as the note's outline).
QString toText(const Value &value, Context &ctx, int depth = 0);
QString formatNumber(double n);
QString formatDate(const DateValue &d);
QString formatDuration(Duration d);

/// Date words of date(…): today, now, tomorrow, yesterday, sow, eow, som, eom, soy, eoy.
bool isDateWord(const QString &word);
std::optional<DateValue> dateWord(const QString &word, const Options &options);

} // namespace brulion::dataview
