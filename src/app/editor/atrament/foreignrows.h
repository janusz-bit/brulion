// Foreign rows (docs/ATRAMENT.md "Rows Atrament does not draw yet"): the rows of a page PageView
// does not draw — code, tables, math, callouts, pictures, queries, the properties… — and the
// page's header and footer, shown by QML items the host places over the frame.
//
// PageView paints each of them as an empty box (no element of its own, no node for screen readers:
// Qt Quick's items have theirs) of the height its QML item has, and records where the box went:
// `PageView::foreignFrames()`, read by the host after every frame — ForeignRowHost (foreignhost.h)
// places the editor's BlockDelegate over each foreign row, the header's and the footer's items
// over theirs, in the same polish (they scroll with the list, in the same frame), and tells the
// page their heights (`PageView::setForeignHeight()`, `setHeaderHeight()`, `setFooterHeight()`):
// a foreign row is measured by its delegate, never by Atrament's layout.
#pragma once

#include "base/geometry.h"

#include <QtGlobal>

#include <cstdint>

namespace brulion::editor::atr {

// Where a frame painted a foreign row, the header or the footer.
struct ForeignFrame {
    enum class Kind : std::uint8_t { Row, Header, Footer };
    Kind kind = Kind::Row;
    int row = -1;          // the model row (Row)
    quint64 uid = 0;       // its block's uid (Row)
    atrament::Rect bounds; // window logical px
    atrament::Rect mask;   // the part of the window the list shows (its rows' content mask)
};

} // namespace brulion::editor::atr
