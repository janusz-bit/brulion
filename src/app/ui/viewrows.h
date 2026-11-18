// placedRow — which row of an item view (ListView, GridView) a child of its content item shows,
// through the view's public QML API only (the delegate's `index`, indexAt, itemAtIndex). Used by
// the scroll probe (scrollprobe.cpp: its anchor and the delegates it follows); tests:
// tests/app/scroll/tst_view_rows.cpp.
//
// Why not the child's place or `visible`: ListView with reuseItems keeps a released delegate in
// its content item where its row was — culled, so not drawn, but `visible` — until it takes the
// delegate for another row, and a delegate still incubating waits at y 0. A probe that took such
// a delegate for a row saw it jump by hundreds of pixels when the view placed it elsewhere,
// though nothing on screen moved (docs/PERFORMANCE.md "scroll-jumps: an anchor in the reuse
// pool"). Only the view knows which children are its rows.
#pragma once

#include <optional>

class QQuickItem;

namespace brulion::ui {

/// The row the item view `view` placed `delegate` (a child of its content item) at: the delegate's
/// `index` property (`required property int index`) or, for a delegate without one (Tasks' rows),
/// the row at the delegate's centre (indexAt) — when the view's item of that row (itemAtIndex) is
/// this delegate. std::nullopt when the view has not placed it (still incubating, or kept for
/// reuse). When `view` is no item view (a plain Flickable: every child counts) the delegate's
/// `index`, else -1; -1 also for a delegate without an index and of no size in an item view (a
/// hidden row: no point of it is the view's).
std::optional<int> placedRow(QQuickItem *view, const QQuickItem *delegate);

} // namespace brulion::ui
