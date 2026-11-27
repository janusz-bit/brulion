#include "tablelayout.h"

#include "blockviews.h"

#include <QVariantList>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::editor::atr {

namespace {

TableAlign alignOf(const QString &name)
{
    if (name == u"left")
        return TableAlign::Left;
    if (name == u"center")
        return TableAlign::Center;
    if (name == u"right")
        return TableAlign::Right;
    return TableAlign::Default;
}

TableGridCell cellOf(const QVariantMap &map)
{
    TableGridCell c;
    c.text = map.value(u"text"_s).toString();
    c.display = map.value(u"display"_s).toString();
    c.html = map.value(u"html"_s).toString();
    c.plain = map.contains(u"plain"_s) ? map.value(u"plain"_s).toString() : c.display;
    c.error = map.value(u"error"_s).toString();
    c.formula = map.value(u"formula"_s).toBool();
    return c;
}

} // namespace

const TableGridCell *TableGrid::cell(int r, int c) const
{
    if (c < 0 || c >= columnCount())
        return nullptr;
    if (r == -1)
        return &header[std::size_t(c)];
    if (r < 0 || r >= rowCount())
        return nullptr;
    return &rows[std::size_t(r)][std::size_t(c)];
}

TableAlign TableGrid::align(int c) const
{
    return c >= 0 && std::size_t(c) < aligns.size() ? aligns[std::size_t(c)] : TableAlign::Default;
}

bool TableGrid::rowShown(int r) const
{
    // (No entry: no filters, every row shows.)
    return r >= 0 && (std::size_t(r) >= shown.size() || shown[std::size_t(r)]);
}

std::vector<int> TableGrid::shownRows(int editRow) const
{
    std::vector<int> out;
    out.reserve(rows.size());
    for (int r = 0; r < rowCount(); ++r) {
        if (rowShown(r) || r == editRow)
            out.push_back(r);
    }
    return out;
}

int TableGrid::nextShownRow(int r, int step) const
{
    if (step == 0)
        return r;
    for (int n = r + step; n >= 0 && n < rowCount(); n += step) {
        if (rowShown(n))
            return n;
    }
    return step > 0 ? rowCount() : -1;
}

QList<int> TableGrid::rowsFrom(int r) const
{
    QList<int> out;
    const int lo = std::max(-1, r);
    for (int n = lo; n < rowCount(); ++n) {
        if (n == lo || n == rowCount() - 1 || n == -1 || rowShown(n))
            out.append(n);
    }
    return out;
}

bool TableGrid::filtered(int c) const
{
    return c >= 0 && std::size_t(c) < filters.size() && !filters[std::size_t(c)].trimmed().isEmpty();
}

QList<int> TableGrid::rowsBetween(int from, int to) const
{
    QList<int> out;
    const int lo = std::max(-1, std::min(from, to));
    const int hi = std::min(rowCount() - 1, std::max(from, to));
    for (int n = lo; n <= hi; ++n) {
        if (n == lo || n == hi || n == -1 || rowShown(n))
            out.append(n);
    }
    return out;
}

int TableGrid::rowAfter(int r, int steps) const
{
    const QList<int> down = rowsFrom(r);
    return down.isEmpty() ? r : down.at(std::clamp(steps, 0, int(down.size()) - 1));
}

TableGrid readTableGrid(const QVariantMap &data)
{
    TableGrid g;
    const QStringList header = data.value(u"header"_s).toStringList();
    if (header.isEmpty())
        return g;
    const QVariantList headerHtml = data.value(u"headerHtml"_s).toList();
    g.header.reserve(std::size_t(header.size()));
    for (qsizetype c = 0; c < header.size(); ++c) {
        // (BlockViews::tableCell's header cell: its lines one after another as what it shows.)
        TableGridCell h;
        h.text = header.at(c);
        h.display = tableCellLines(h.text).join(u' ');
        h.plain = h.display;
        if (c < headerHtml.size())
            h.html = headerHtml.at(c).toString();
        g.header.push_back(std::move(h));
    }
    const QVariantList aligns = data.value(u"aligns"_s).toList();
    g.aligns.reserve(std::size_t(header.size()));
    for (qsizetype c = 0; c < header.size(); ++c)
        g.aligns.push_back(c < aligns.size() ? alignOf(aligns.at(c).toString()) : TableAlign::Default);
    const QVariantList rows = data.value(u"rows"_s).toList();
    g.rows.reserve(std::size_t(rows.size()));
    for (const QVariant &row : rows) {
        const QVariantList cells = row.toList();
        std::vector<TableGridCell> out;
        out.reserve(std::size_t(header.size()));
        for (qsizetype c = 0; c < header.size(); ++c)
            out.push_back(c < cells.size() ? cellOf(cells.at(c).toMap()) : TableGridCell{});
        g.rows.push_back(std::move(out));
    }
    const QVariantList shown = data.value(u"shown"_s).toList();
    g.shown.reserve(std::size_t(shown.size()));
    for (const QVariant &s : shown)
        g.shown.push_back(!(s.typeId() == QMetaType::Bool && !s.toBool()));
    g.headerLines = std::clamp(data.value(u"headerLines"_s, 1).toInt(), 1, 3);
    g.filtering = data.value(u"filtering"_s).toBool();
    const QStringList filters = data.value(u"filters"_s).toStringList();
    g.filters.reserve(std::size_t(header.size()));
    for (qsizetype c = 0; c < header.size(); ++c)
        g.filters.push_back(c < filters.size() ? filters.at(c) : QString());
    g.shownCount = data.contains(u"shownCount"_s) ? data.value(u"shownCount"_s).toInt() : g.rowCount();
    return g;
}

std::vector<float> tableColumnWidths(const TableGrid &grid, const TableMeasure &measure, float padding)
{
    std::vector<float> out;
    out.reserve(grid.header.size());
    for (int c = 0; c < grid.columnCount(); ++c) {
        // A header of several lines (`<br>`) is as wide as its widest line — of its Markdown, as
        // TableBlockView measures it.
        float w = 0;
        for (const QString &line : tableCellLines(grid.header[std::size_t(c)].text))
            w = std::max(w, measure(line, true));
        for (const std::vector<TableGridCell> &row : grid.rows) {
            const TableGridCell &cell = row[std::size_t(c)];
            if (!cell.plain.isEmpty())
                w = std::max(w, measure(cell.plain, false));
        }
        out.push_back(std::clamp(std::ceil(w) + 2 * padding, 56.0f, 280.0f));
    }
    return out;
}

TableAlign shownAlign(const TableGrid &grid, int r, int c)
{
    const TableAlign a = grid.align(c);
    const TableGridCell *cell = grid.cell(r, c);
    if (a == TableAlign::Default && cell && cell->formula)
        return TableAlign::Right;
    return a == TableAlign::Default ? TableAlign::Left : a;
}

} // namespace brulion::editor::atr
