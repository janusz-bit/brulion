#include "blockmove.h"

namespace brulion::editor {

using markdown::BlockDocument;
using markdown::BlockEntry;
using brulion::BlockKind;
using markdown::EditResult;

namespace {

// The blocks moved: their top blocks' first and last uids and how many tops there are.
struct Moving {
    quint64 first = 0;
    quint64 last = 0;
    qsizetype count = 0;
};

int rowOf(const BlockDocument &d, quint64 uid)
{
    return uid ? d.indexOfUid(uid) : -1;
}

// Rows [first, end) of the blocks moved (none: blocks from elsewhere).
struct Rows {
    int first = -1;
    int end = -1;
    bool contains(int row) const { return row >= first && row < end; }
    bool inside(int gap) const { return gap > first && gap < end; }
};

// The first of row `t`'s siblings (BlockDocument's own rule: the run of blocks of its depth and
// parent, a shallower block ending it).
int firstSibling(const BlockDocument &d, int t)
{
    const BlockEntry &own = d.at(t);
    int first = t;
    for (int j = t - 1; j >= 0; --j) {
        const BlockEntry &e = d.at(j);
        if (e.depth < own.depth || (e.depth == own.depth && e.parent != own.parent))
            break;
        if (e.depth == own.depth)
            first = j;
    }
    return first;
}

// The siblings of row `t` in order, the page's properties left out.
QList<int> siblingsOf(const BlockDocument &d, int t)
{
    const BlockEntry &own = d.at(t);
    QList<int> out;
    for (int j = firstSibling(d, t); j < d.count(); j = d.subtreeRange(j).last() + 1) {
        const BlockEntry &e = d.at(j);
        if (e.depth != own.depth || e.parent != own.parent)
            break;
        if (e.kind != BlockKind::Frontmatter)
            out.append(j);
    }
    return out;
}

// The top blocks of [first, last] as consecutive siblings; nothing otherwise.
std::optional<Moving> movingTops(const BlockDocument &d, int first, int last)
{
    const QList<int> tops = d.rangeTops(first, last);
    if (tops.isEmpty())
        return std::nullopt;
    const QList<int> siblings = siblingsOf(d, tops.first());
    const qsizetype a = siblings.indexOf(tops.first());
    if (a < 0 || siblings.mid(a, tops.size()) != tops)
        return std::nullopt;
    return Moving{d.at(tops.first()).uid, d.at(tops.last()).uid, tops.size()};
}

// (A step BlockDocument refuses leaves the blocks short of the place: arrived() tells.)
void step(BlockDocument &d, const Moving &m, MoveStep s, QList<MoveStep> &steps)
{
    applyMoveStep(d, m.first, m.last, s);
    steps.append(s);
}

// Moves the blocks among their siblings until they start at sibling `gap` of the list as it is now
// (the blocks in it; its size: after the last one).
void slideTo(BlockDocument &d, const Moving &m, qsizetype gap, QList<MoveStep> &steps)
{
    const QList<int> siblings = siblingsOf(d, rowOf(d, m.first));
    const qsizetype a = siblings.indexOf(rowOf(d, m.first));
    const qsizetype n = gap < a ? a - gap : std::max<qsizetype>(0, gap - (a + m.count));
    for (qsizetype k = 0; k < n; ++k)
        step(d, m, gap < a ? MoveStep::Up : MoveStep::Down, steps);
}

// Whether `a` (−1: the top level) is an ancestor of `b`.
bool isAncestor(const BlockDocument &d, int a, int b)
{
    if (b < 0)
        return false;
    for (int r = d.parent(b);; r = d.parent(r)) {
        if (r == a)
            return true;
        if (r < 0)
            return false;
    }
}

// The ancestor of `target` (or itself) that is a child of `parent` (−1: the top level).
int childOnPath(const BlockDocument &d, int parent, int target)
{
    int r = target;
    while (r >= 0 && d.parent(r) != parent)
        r = d.parent(r);
    return r;
}

// One level from the blocks' parent `current` towards `target` (−1: the top level): deeper by
// sliding after the child of their parent on the way and Tab, shallower by sliding to the end of
// their siblings and Shift+Tab (no sibling after them to take along).
void levelTowards(BlockDocument &d, const Moving &m, int current, int target, QList<MoveStep> &steps)
{
    const int t = rowOf(d, m.first);
    if (isAncestor(d, current, target)) {
        slideTo(d, m, siblingsOf(d, t).indexOf(childOnPath(d, current, target)) + 1, steps);
        step(d, m, MoveStep::Indent, steps);
    } else {
        slideTo(d, m, siblingsOf(d, t).size(), steps);
        step(d, m, MoveStep::Outdent, steps);
    }
}

// Level by level until the blocks are children of `parent` (0: the top level) — a level a pass,
// and no outline is deeper than its rows.
void reachParent(BlockDocument &d, const Moving &m, quint64 parent, QList<MoveStep> &steps)
{
    for (int pass = 0; pass < d.count() && d.parent(rowOf(d, m.first)) != rowOf(d, parent); ++pass)
        levelTowards(d, m, d.parent(rowOf(d, m.first)), rowOf(d, parent), steps);
}

// Among their siblings, before `before` (0: after the last one).
void slideBefore(BlockDocument &d, const Moving &m, quint64 before, QList<MoveStep> &steps)
{
    const QList<int> siblings = siblingsOf(d, rowOf(d, m.first));
    slideTo(d, m, before ? siblings.indexOf(rowOf(d, before)) : siblings.size(), steps);
}

// The blocks are where the place puts them: children of its parent, `depth` deep, right before its
// sibling (none after them when it has none).
bool arrived(const BlockDocument &d, const Moving &m, PlaceTarget target, int depth)
{
    const int first = rowOf(d, m.first);
    const int next = d.subtreeRange(rowOf(d, m.last)).last() + 1;
    const quint64 after = next < d.count() && d.at(next).depth == depth ? d.at(next).uid : 0;
    return d.parent(first) == rowOf(d, target.parent) && d.at(first).depth == depth && after == target.before;
}

// The parent of a place: the nearest block before the gap less deep than it — one level less, a
// list item, none of the blocks moved (−1: the top level); nothing when there is no such block.
std::optional<int> placeParent(const BlockDocument &doc, BlockPlace place, Rows moved)
{
    if (place.depth == 0)
        return -1;
    int r = place.gap - 1;
    while (r >= 0 && doc.at(r).depth >= place.depth)
        --r;
    if (r < 0 || doc.at(r).depth != place.depth - 1 || doc.at(r).kind != BlockKind::ListItem || moved.contains(r))
        return std::nullopt;
    return r;
}

// The sibling a place puts blocks before — the block after it, the moved ones jumped over — or 0 at
// the end of the parent's children; nothing when that block is deeper (its parent would change) or
// the page's properties.
std::optional<quint64> placeBefore(const BlockDocument &doc, BlockPlace place, Rows moved)
{
    const int next = place.gap == moved.first ? moved.end : place.gap;
    if (next >= doc.count())
        return quint64(0);
    const BlockEntry &e = doc.at(next);
    if (e.depth > place.depth || e.kind == BlockKind::Frontmatter)
        return std::nullopt;
    // (As deep as the place: a child of its parent too — a block between them would be shallower.)
    return e.depth == place.depth ? e.uid : quint64(0);
}

// A gap of the document (none inside the blocks moved), a depth.
bool placeInDocument(const BlockDocument &doc, BlockPlace place, Rows moved)
{
    return place.gap >= 0 && place.gap <= doc.count() && place.depth >= 0 && !moved.inside(place.gap);
}

// Every top block of `rows` (each with its subtree) a list item.
bool listItemsOnly(const BlockDocument &doc, Rows rows)
{
    for (int r = rows.first; r < rows.end; r = doc.subtreeRange(r).last() + 1) {
        if (doc.at(r).kind != BlockKind::ListItem)
            return false;
    }
    return true;
}

} // namespace

std::optional<PlaceTarget> placeTarget(const BlockDocument &doc, BlockPlace place, int movedFirst, int movedEnd)
{
    // (No blocks moved: rows (−1, −1), which hold none.)
    const Rows moved{movedFirst, movedEnd};
    if (!placeInDocument(doc, place, moved))
        return std::nullopt;
    const std::optional<int> parent = placeParent(doc, place, moved);
    const std::optional<quint64> before = parent ? placeBefore(doc, place, moved) : std::nullopt;
    if (!before)
        return std::nullopt;
    return PlaceTarget{*parent >= 0 ? doc.at(*parent).uid : 0, *before};
}

std::optional<QList<MoveStep>> planBlockMove(const BlockDocument &doc, int first, int last, BlockPlace place)
{
    const std::optional<Moving> m = movingTops(doc, first, last);
    if (!m)
        return std::nullopt;
    const Rows moved{rowOf(doc, m->first), doc.subtreeRange(rowOf(doc, m->last)).last() + 1};
    // (A paragraph, a heading, a table… stays at the top level.)
    if (place.depth > 0 && !listItemsOnly(doc, moved))
        return std::nullopt;
    const std::optional<PlaceTarget> target = placeTarget(doc, place, moved.first, moved.end);
    if (!target)
        return std::nullopt;
    BlockDocument d = doc;
    QList<MoveStep> steps;
    reachParent(d, *m, target->parent, steps);
    slideBefore(d, *m, target->before, steps);
    if (steps.isEmpty() || !arrived(d, *m, *target, place.depth))
        return std::nullopt;
    return steps;
}

EditResult applyMoveStep(BlockDocument &doc, quint64 firstTop, quint64 lastTop, MoveStep s)
{
    // (BlockDocument refuses rows that are none: an unknown uid moves nothing.)
    using Range = EditResult (BlockDocument::*)(int, int);
    static constexpr Range ranges[] = {&BlockDocument::moveRangeUp, &BlockDocument::moveRangeDown,
                                       &BlockDocument::indentRange, &BlockDocument::outdentRange};
    return (doc.*ranges[std::size_t(s)])(rowOf(doc, firstTop), rowOf(doc, lastTop));
}

} // namespace brulion::editor
