// Evaluation of Dataview expressions (eval.cpp), the built-in functions (functions.cpp) and the
// page, file and task objects rows are made of (objects.cpp). Internal to the dataview module.
#pragma once

#include "dataview/ast_p.h"
#include "dataview/dataview.h"

#include <QHash>
#include <QMap>
#include <QString>

#include <memory>
#include <optional>

namespace brulion::dataview {

/// Thrown when an evaluation must stop (too costly, too many rows): caught by runQuery().
struct EvalError {
    QString message;
};

/// What every evaluation of one run shares: the index, the options, the objects made so far.
class Context {
public:
    Context(const VaultIndex &index, const Options &options);

    const VaultIndex &index() const { return m_index; }
    const Options &options() const { return m_options; }
    QDate today() const { return m_options.now.date(); }

    /// One step of work; throws EvalError past Options::maxSteps.
    void step(qint64 n = 1)
    {
        m_steps += n;
        if (m_steps > m_options.maxSteps)
            throwTooCostly();
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
    [[noreturn]] static void throwTooCostly();

    const VaultIndex &m_index;
    const Options &m_options;
    qint64 m_steps = 0;
    QHash<VaultPath, Value> m_pages;
    QHash<QString, Value> m_items; ///< "path\x1fblock"
    std::optional<QHash<QString, QString>> m_tagDisplay;
};

/// A row while the query runs: the page or task it came from, the object its fields are read
/// from, and the fields FLATTEN and GROUP BY added on top.
struct RowData {
    VaultPath path;
    int blockIndex = -1;
    Value base;                          ///< page / task object, or a group object
    QList<std::pair<QString, Value>> extra; ///< names set by FLATTEN / GROUP BY (latest last)
    bool group = false;
    Value key;                           ///< group rows: the key
    QList<RowData> rows;                 ///< group rows: the rows grouped
};

/// The row as an object: its `extra` names first, then the base object.
Value rowObject(const RowData &row);

/// A lambda: parameters, body and the names it was written among.
struct Function {
    QStringList params;
    ExprPtr body;
    Value row;                              ///< the row it was written in
    QList<std::pair<QString, Value>> bound; ///< parameters of enclosing lambdas
};

/// Evaluate `expr` for a row object (`row`; null outside rows) with `bound` names in scope.
Value evaluate(const Expr &expr, Context &ctx, const Value &row, const QList<std::pair<QString, Value>> &bound = {});
/// Call a function value with arguments.
Value callFunction(const Function &f, const ValueList &args, Context &ctx);
/// A field of a value: an object's field, a link's page field, a date's or duration's part, a
/// list's elements' fields (flattened one level). Null when missing.
Value fieldOf(const Value &value, const QString &key, Context &ctx);

/// A built-in function: its canonical name, its arity, its body.
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
/// dates ISO, numbers without trailing zeros.
QString toText(const Value &value, Context &ctx);
QString formatNumber(double n);
QString formatDate(const DateValue &d);
QString formatDuration(const Duration &d);

/// Date words of date(…): today, now, tomorrow, yesterday, sow, eow, som, eom, soy, eoy.
bool isDateWord(const QString &word);
std::optional<DateValue> dateWord(const QString &word, const Options &options);

} // namespace brulion::dataview
