// Table formulas (SYNTAX.md §9): every body cell starting with '=' is evaluated, references
// first. Dependencies are found with an iterative Tarjan SCC over lazily enumerated edges, so
// long reference chains cannot overflow the stack and dense graphs (`=sum(above)` in every row)
// are never materialised; cycles are reported, everything is bounded by a work budget.
#include "calc/table.h"

#include "calc/evaluator_p.h"
#include "calc/lexer_p.h"
#include "calc/parser_p.h"
#include "calc/util_p.h"

#include <QHash>

#include <algorithm>
#include <vector>

namespace brulion::calc {

namespace {

bool isFormula(QStringView text)
{
    return text.startsWith(u'=') && !text.mid(1).trimmed().isEmpty();
}

/// Rectangle of cells covered by a reference; `skip` is a cell left out (the formula's own).
struct Area {
    int r1 = 0;
    int r2 = -1;
    int c1 = 0;
    int c2 = -1;
    qsizetype skip = -1;

    qint64 size() const { return qint64(std::max(0, r2 - r1 + 1)) * std::max(0, c2 - c1 + 1); }
};

/// The table's cells (trimmed once), formula flags and plain values parsed at most once.
class Grid {
public:
    Grid(const TableModel &model, int cols, CalcLocale locale)
        : m_rows(static_cast<int>(model.rows.size()))
        , m_cols(cols)
        , m_locale(locale)
        , m_formulaRows(static_cast<std::size_t>(cols))
    {
        m_texts.reserve(qsizetype(m_rows) * cols);
        m_formulaIds.assign(static_cast<std::size_t>(m_rows) * static_cast<std::size_t>(cols), -1);
        for (int r = 0; r < m_rows; ++r) {
            const QStringList &row = model.rows.at(r);
            for (int c = 0; c < cols; ++c) {
                const QString text = c < row.size() ? row.at(c).trimmed() : QString();
                m_texts.append(text);
                if (isFormula(text)) {
                    m_formulaIds[static_cast<std::size_t>(key(r, c))] = static_cast<int>(m_formulaKeys.size());
                    m_formulaKeys.push_back(key(r, c));
                    m_formulaRows[static_cast<std::size_t>(c)].push_back(r);
                }
            }
        }
    }

    int rows() const { return m_rows; }
    int cols() const { return m_cols; }
    qsizetype key(int r, int c) const { return qsizetype(r) * m_cols + c; }
    int rowOf(qsizetype key) const { return static_cast<int>(key / m_cols); }
    int colOf(qsizetype key) const { return static_cast<int>(key % m_cols); }
    const QString &text(int r, int c) const { return m_texts.at(key(r, c)); }
    /// Formula id of a cell, -1 for plain cells.
    int formulaId(int r, int c) const { return m_formulaIds[static_cast<std::size_t>(key(r, c))]; }
    const std::vector<qsizetype> &formulaKeys() const { return m_formulaKeys; }
    const std::vector<int> &formulaRows(int c) const { return m_formulaRows[static_cast<std::size_t>(c)]; }

    /// Value of a plain cell: None when empty, a number when it parses, else the text.
    const Value &plainValue(int r, int c)
    {
        const qsizetype k = key(r, c);
        auto it = m_plain.find(k);
        if (it == m_plain.end()) {
            const QString &t = text(r, c);
            Value v;
            if (!t.isEmpty()) {
                if (const std::optional<ParsedNumber> n = parseLocaleNumber(t, m_locale)) {
                    v = n->percent ? Value::percent(n->value) : Value::number(n->value);
                    if (n->currency.isValid())
                        v.setCurrency(n->currency);
                } else {
                    v = Value::string(stripInlineMarkup(t));
                }
            }
            it = m_plain.insert(k, v);
        }
        return *it;
    }

private:
    int m_rows;
    int m_cols;
    CalcLocale m_locale;
    QStringList m_texts;
    std::vector<int> m_formulaIds;
    std::vector<qsizetype> m_formulaKeys;
    std::vector<std::vector<int>> m_formulaRows; ///< rows of formula cells per column, ascending
    QHash<qsizetype, Value> m_plain;
};

/// Cells covered by a reference from cell (r, c); ranges and directions are clamped.
std::optional<Area> areaOf(const TableRef &ref, int r, int c, const Grid &grid, CalcError &err)
{
    switch (ref.kind) {
    case TableRef::Kind::Cell:
        if (ref.row1 < 0 || ref.row1 >= grid.rows() || ref.col1 < 0 || ref.col1 >= grid.cols()) {
            err = {CalcErrorCode::RefOutside, ref.text};
            return std::nullopt;
        }
        return Area{ref.row1, ref.row1, ref.col1, ref.col1, -1};
    case TableRef::Kind::Header:
        return Area{r, r, ref.col1, ref.col1, -1};
    case TableRef::Kind::Range:
        return Area{std::max(0, std::min(ref.row1, ref.row2)), std::min(grid.rows() - 1, std::max(ref.row1, ref.row2)),
                    std::max(0, std::min(ref.col1, ref.col2)), std::min(grid.cols() - 1, std::max(ref.col1, ref.col2)),
                    grid.key(r, c)};
    case TableRef::Kind::Direction:
        switch (ref.direction) {
        case TableRef::Direction::Above:
            return Area{0, r - 1, c, c, -1};
        case TableRef::Direction::Below:
            return Area{r + 1, grid.rows() - 1, c, c, -1};
        case TableRef::Direction::Left:
            return Area{r, r, 0, c - 1, -1};
        case TableRef::Direction::Right:
            return Area{r, r, c + 1, grid.cols() - 1, -1};
        }
    }
    err = {CalcErrorCode::Generic, {}};
    return std::nullopt;
}

bool isSingle(const TableRef &ref)
{
    return ref.kind == TableRef::Kind::Cell || ref.kind == TableRef::Kind::Header;
}

struct Formula {
    int r = 0;
    int c = 0;
    ParsedProgram program;
    CalcError error;
};

/// Lazily enumerates the formula cells a formula depends on.
struct EdgeCursor {
    qsizetype ref = 0;
    bool ready = false;
    Area area;
    int col = 0;
    std::size_t pos = 0;
};

std::size_t lowerBound(const std::vector<int> &rows, int value)
{
    return static_cast<std::size_t>(std::lower_bound(rows.begin(), rows.end(), value) - rows.begin());
}

/// Next dependency (a formula id) of `f`, or -1 when there are no more.
int nextEdge(const Formula &f, EdgeCursor &cur, const Grid &grid)
{
    const QList<TableRef> &refs = f.program.refs;
    while (cur.ref < refs.size()) {
        if (!cur.ready) {
            CalcError err;
            const std::optional<Area> area = areaOf(refs.at(cur.ref), f.r, f.c, grid, err);
            if (!area) {
                ++cur.ref;
                continue;
            }
            cur.area = *area;
            cur.col = std::max(0, cur.area.c1);
            if (cur.col <= cur.area.c2)
                cur.pos = lowerBound(grid.formulaRows(cur.col), cur.area.r1);
            cur.ready = true;
        }
        while (cur.col <= std::min(grid.cols() - 1, cur.area.c2)) {
            const std::vector<int> &rows = grid.formulaRows(cur.col);
            if (cur.pos < rows.size() && rows[cur.pos] <= cur.area.r2) {
                const int row = rows[cur.pos++];
                if (grid.key(row, cur.col) == cur.area.skip)
                    continue;
                return grid.formulaId(row, cur.col);
            }
            ++cur.col;
            if (cur.col <= std::min(grid.cols() - 1, cur.area.c2))
                cur.pos = lowerBound(grid.formulaRows(cur.col), cur.area.r1);
        }
        ++cur.ref;
        cur.ready = false;
    }
    return -1;
}

enum class CellState : quint8 { Pending, Ok, Error, Cycle };

class TableEvaluator {
public:
    TableEvaluator(const TableModel &model, CalcLocale locale, const Scope &scope)
        : m_locale(locale)
        , m_scope(scope)
        , m_grid(model, columnCount(model), locale)
    {
    }

    TableEval run(const TableModel &model)
    {
        const int cols = m_grid.cols();
        for (int c = 0; c < cols; ++c)
            m_headers.append(normalizeHeader(c < model.header.size() ? QStringView(model.header.at(c)) : QStringView()));
        m_result.values.reserve(m_grid.rows());
        m_result.errors.reserve(m_grid.rows());
        for (int r = 0; r < m_grid.rows(); ++r) {
            m_result.values.append(QStringList(cols, QString()));
            m_result.errors.append(QStringList(cols, QString()));
        }
        parseFormulas();
        m_states.assign(m_formulas.size(), CellState::Pending);
        m_values.resize(m_formulas.size());
        stronglyConnected();
        return std::move(m_result);
    }

private:
    static int columnCount(const TableModel &model)
    {
        qsizetype cols = model.header.size();
        for (const QStringList &r : model.rows)
            cols = std::max(cols, r.size());
        return static_cast<int>(cols);
    }

    void parseFormulas()
    {
        ParseOptions options;
        options.statements = false;
        options.tableRefs = true;
        options.headers = m_headers;
        for (const qsizetype k : m_grid.formulaKeys()) {
            Formula f;
            f.r = m_grid.rowOf(k);
            f.c = m_grid.colOf(k);
            const QStringView text = QStringView(m_grid.text(f.r, f.c)).mid(1);
            if (text.size() > kMaxExpressionLength) {
                f.error = {CalcErrorCode::TooLong, {}};
            } else {
                const QString source = detail::toNfc(text);
                QList<Token> tokens = lex(source);
                normalizeNumbers(tokens, source, m_locale);
                f.program = parseProgram(tokens, source, options);
                f.error = f.program.error;
                if (!f.error && f.program.statements.empty())
                    f.error = {CalcErrorCode::ValueExpected, {}};
                for (const TableRef &ref : f.program.refs) {
                    if (f.error)
                        break;
                    areaOf(ref, f.r, f.c, m_grid, f.error); // references outside the table
                }
            }
            m_formulas.push_back(std::move(f));
        }
    }

    /// Tarjan's SCC, iterative; components are evaluated as soon as they are complete
    /// (dependencies first), so a huge table that exhausts the budget still gets its first
    /// formulas computed.
    void stronglyConnected()
    {
        const std::size_t n = m_formulas.size();
        std::vector<int> index(n, -1);
        std::vector<int> low(n, 0);
        std::vector<bool> onStack(n, false);
        std::vector<bool> selfLoop(n, false);
        std::vector<int> stack;
        struct Frame {
            int v;
            EdgeCursor cursor;
        };
        std::vector<Frame> work;
        int counter = 0;
        const auto open = [&](int v) {
            index[static_cast<std::size_t>(v)] = low[static_cast<std::size_t>(v)] = counter++;
            stack.push_back(v);
            onStack[static_cast<std::size_t>(v)] = true;
            work.push_back({v, {}});
        };
        for (int root = 0; root < static_cast<int>(n); ++root) {
            if (index[static_cast<std::size_t>(root)] >= 0)
                continue;
            open(root);
            while (!work.empty()) {
                const int v = work.back().v;
                const auto vi = static_cast<std::size_t>(v);
                Formula &f = m_formulas[vi];
                int w = -1;
                if (!f.error) {
                    if (!m_budget.chargeTotal(1))
                        f.error = {CalcErrorCode::TooComplex, {}};
                    else
                        w = nextEdge(f, work.back().cursor, m_grid);
                }
                if (w >= 0) {
                    const auto wi = static_cast<std::size_t>(w);
                    if (w == v)
                        selfLoop[wi] = true;
                    if (index[wi] < 0)
                        open(w);
                    else if (onStack[wi])
                        low[vi] = std::min(low[vi], index[wi]);
                    continue;
                }
                work.pop_back();
                if (low[vi] == index[vi]) {
                    std::vector<int> component;
                    int x = -1;
                    do {
                        x = stack.back();
                        stack.pop_back();
                        onStack[static_cast<std::size_t>(x)] = false;
                        component.push_back(x);
                    } while (x != v);
                    evaluateComponent(component, component.size() > 1 || selfLoop[vi]);
                }
                if (!work.empty()) {
                    const auto pi = static_cast<std::size_t>(work.back().v);
                    low[pi] = std::min(low[pi], low[vi]);
                }
            }
        }
    }

    void evaluateComponent(const std::vector<int> &component, bool cyclic)
    {
        for (const int id : component) {
            const auto i = static_cast<std::size_t>(id);
            Formula &f = m_formulas[i];
            QString &value = m_result.values[f.r][f.c];
            QString &error = m_result.errors[f.r][f.c];
            if (cyclic) {
                m_states[i] = CellState::Cycle;
                value = kCycleMarker.toString();
                error = CalcError{CalcErrorCode::Cycle, {}}.message(m_locale);
                continue;
            }
            CalcError err = f.error;
            Value v;
            if (!err)
                v = evaluateFormula(f, err);
            if (err) {
                m_states[i] = CellState::Error;
                value = kErrorMarker.toString();
                error = err.message(m_locale);
                continue;
            }
            m_states[i] = CellState::Ok;
            value = v.isNone() ? QStringLiteral("") : v.format(m_locale);
            m_values[i] = std::move(v);
        }
    }

    /// Value of a referenced cell: plain value, or the result of an evaluated formula.
    std::optional<Value> cellValue(int r, int c, CalcError &err)
    {
        const int id = m_grid.formulaId(r, c);
        if (id < 0)
            return m_grid.plainValue(r, c);
        if (m_states[static_cast<std::size_t>(id)] == CellState::Ok)
            return m_values[static_cast<std::size_t>(id)];
        err = {CalcErrorCode::RefError, cellName(r, c)};
        return std::nullopt;
    }

    Value resolve(const Formula &f, int refIndex, CalcError &err)
    {
        const TableRef &ref = f.program.refs.at(refIndex);
        const std::optional<Area> area = areaOf(ref, f.r, f.c, m_grid, err);
        if (!area)
            return {};
        if (isSingle(ref)) {
            const std::optional<Value> v = cellValue(area->r1, area->c1, err);
            if (!v)
                return {};
            return v->isNone() ? Value::number(0) : *v;
        }
        if (!m_budget.charge(area->size())) {
            err = {CalcErrorCode::TooComplex, {}};
            return {};
        }
        std::vector<Value> items;
        for (int r = area->r1; r <= area->r2; ++r) {
            for (int c = area->c1; c <= area->c2; ++c) {
                if (m_grid.key(r, c) == area->skip)
                    continue;
                const std::optional<Value> v = cellValue(r, c, err);
                if (!v)
                    return {};
                if (v->isNumber())
                    items.push_back(*v);
            }
        }
        return Value::list(std::move(items));
    }

    Value evaluateFormula(Formula &f, CalcError &err)
    {
        m_budget.startExpression();
        Scope scope = m_scope; // formulas cannot assign; a copy keeps the caller's scope const
        EvalEnv env;
        env.locale = m_locale;
        env.scope = &scope;
        env.budget = &m_budget;
        env.reference = [this, &f](int refIndex, CalcError &e) { return resolve(f, refIndex, e); };
        Evaluator evaluator(std::move(env));
        RunResult run = runProgram(f.program, evaluator);
        if (run.error) {
            err = run.error;
            return {};
        }
        return run.value;
    }

    CalcLocale m_locale;
    const Scope &m_scope;
    Grid m_grid;
    QStringList m_headers;
    std::vector<Formula> m_formulas;
    std::vector<CellState> m_states;
    std::vector<Value> m_values;
    WorkBudget m_budget;
    TableEval m_result;
};

} // namespace

TableEval evaluateTable(const TableModel &model, CalcLocale locale, const Scope &scope)
{
    return TableEvaluator(model, locale, scope).run(model);
}

} // namespace brulion::calc
