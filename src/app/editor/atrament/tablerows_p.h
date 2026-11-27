// PageView's tables (tablerows.cpp, tableselection.cpp, tablefilters.cpp): the table of a row as
// last read — the role's map, its grid, the columns' natural widths and each cell's rendering.
#pragma once

#include "pageview.h"
#include "tablelayout.h"

#include "markdown/renderedtext.h"

#include <QVariantMap>

#include <cstdint>
#include <vector>

namespace brulion::editor::atr {

struct PageView::TableEntry {
    QVariantMap data;
    TableGrid grid;
    std::vector<float> widths;
    std::vector<markdown::RenderedText> header;
    std::vector<std::vector<markdown::RenderedText>> rows;
    std::uint64_t frame = 0;
    const markdown::RenderedText *rendered(int r, int c) const
    {
        if (c < 0 || c >= grid.columnCount())
            return nullptr;
        if (r == -1)
            return &header[std::size_t(c)];
        if (r < 0 || r >= grid.rowCount())
            return nullptr;
        return &rows[std::size_t(r)][std::size_t(c)];
    }
};

} // namespace brulion::editor::atr
