// Moving blocks to another place of their page — a drop of drag to move (docs/ATRAMENT.md "The
// block editor in Atrament (M2)", step 4 "Drag to move"), the outliner's way: between any two rows,
// as deep as the pointer's x says (a child of the block above, a sibling of one of its ancestors).
//
// A move is planned as BlockDocument's own range steps — Alt+↑ / Alt+↓ (moveRangeUp / Down), Tab
// (indentRange) and Shift+Tab at the end of the siblings (outdentRange, which then takes no sibling
// along) — tried on a copy of the document first: so the blocks keep their uids (the selection,
// folds), every byte no step touches stays as it was (line endings, the other blocks, a list's
// marker style and numbering as the steps keep them), and the controller applies the steps as one
// undo step (PageController::moveSelectionTo).
//
//   const auto steps = planBlockMove(doc, range.first, range.last(), {gap, depth});
//   if (steps) for (MoveStep s : *steps) applyMoveStep(doc, firstTopUid, lastTopUid, s);
#pragma once

#include "markdown/blockdocument.h"

#include <QList>

#include <cstdint>
#include <optional>

namespace brulion::editor {

// A place between two rows of a document: before row `gap` (count(): at the end), `depth` deep —
// a child of the last block before the gap that is `depth - 1` deep (depth 0: the top level).
struct BlockPlace {
    int gap = 0;
    int depth = 0;
    friend bool operator==(const BlockPlace &, const BlockPlace &) = default;
};

// Where a place puts blocks: its parent and the sibling they go before (uids; 0: the top level, the
// end of the parent's children). `moving` [movedFirst, movedEnd) are rows being moved there (none
// for blocks coming from elsewhere): the place may not be inside them or under one of them.
struct PlaceTarget {
    quint64 parent = 0;
    quint64 before = 0;
};
std::optional<PlaceTarget> placeTarget(const markdown::BlockDocument &doc, BlockPlace place, int movedFirst = -1,
                                       int movedEnd = -1);

enum class MoveStep : std::uint8_t { Up, Down, Indent, Outdent };

// The steps that take the blocks of rows [first, last] — their top blocks (rangeTops), consecutive
// siblings, each with its subtree — to `place`; nothing when they cannot go there (inside
// themselves, under a block that is no list item, too deep for the block above, above the page's
// properties, a paragraph anywhere but the top level, a place that would take the next block's
// parent away) or are there already.
std::optional<QList<MoveStep>> planBlockMove(const markdown::BlockDocument &doc, int first, int last,
                                             BlockPlace place);

// One step on the blocks whose top blocks run from `firstTop` to `lastTop` (uids).
markdown::EditResult applyMoveStep(markdown::BlockDocument &doc, quint64 firstTop, quint64 lastTop, MoveStep step);

} // namespace brulion::editor
