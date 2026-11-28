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

// The siblings of row `t` in order (BlockDocument's own rule: the run of blocks of its depth and
// parent, a shallower block ending it), the page's properties left out.
QList<int> siblingsOf(const BlockDocument &d, int t)
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
    QList<int> out;
    for (int j = first; j < d.count(); j = d.subtreeRange(j).last() + 1) {
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
    if (a < 0 || a + tops.size() > siblings.size() || siblings.mid(a, tops.size()) != tops)
        return std::nullopt;
    return Moving{d.at(tops.first()).uid, d.at(tops.last()).uid, tops.size()};
}

bool step(BlockDocument &d, const Moving &m, MoveStep s, QList<MoveStep> &steps)
{
    if (!applyMoveStep(d, m.first, m.last, s).changed())
        return false;
    steps.append(s);
    return true;
}

// Moves the blocks among their siblings until they start at sibling `gap` of the list as it is now
// (the blocks in it; its size: after the last one).
bool slideTo(BlockDocument &d, const Moving &m, qsizetype gap, QList<MoveStep> &steps)
{
    const QList<int> siblings = siblingsOf(d, rowOf(d, m.first));
    const qsizetype a = siblings.indexOf(rowOf(d, m.first));
    const qsizetype b = a + m.count - 1;
    if (a < 0 || gap < 0 || gap > siblings.size())
        return false;
    const qsizetype n = gap < a ? a - gap : std::max<qsizetype>(0, gap - (b + 1));
    for (qsizetype k = 0; k < n; ++k) {
        if (!step(d, m, gap < a ? MoveStep::Up : MoveStep::Down, steps))
            return false;
    }
    return true;
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

// Up or down the outline, one level a pass, until the blocks are children of `parent`: deeper by
// sliding after the child of their parent on the way and Tab, shallower by sliding to the end of
// their siblings and Shift+Tab (no sibling after them to take along).
bool reachParent(BlockDocument &d, const Moving &m, quint64 parent, QList<MoveStep> &steps)
{
    for (int pass = 0; pass <= d.count(); ++pass) {
        const int t = rowOf(d, m.first);
        const int current = d.parent(t);
        const int target = rowOf(d, parent);
        if (current == target)
            return true;
        if (isAncestor(d, current, target)) {
            const qsizetype k = siblingsOf(d, t).indexOf(childOnPath(d, current, target));
            if (k < 0 || !slideTo(d, m, k + 1, steps) || !step(d, m, MoveStep::Indent, steps))
                return false;
        } else if (!slideTo(d, m, siblingsOf(d, t).size(), steps) || !step(d, m, MoveStep::Outdent, steps)) {
            return false;
        }
    }
    return false;
}

} // namespace

std::optional<PlaceTarget> placeTarget(const BlockDocument &doc, BlockPlace place, int movedFirst, int movedEnd)
{
    const bool moving = movedFirst >= 0 && movedEnd > movedFirst;
    if (place.gap < 0 || place.gap > doc.count() || place.depth < 0
        || (moving && place.gap > movedFirst && place.gap < movedEnd))
        return std::nullopt;
    // The parent: the nearest block before the gap less deep than the place (one level less, a list
    // item, none of the blocks moved).
    int parent = -1;
    if (place.depth > 0) {
        int r = place.gap - 1;
        while (r >= 0 && doc.at(r).depth >= place.depth)
            --r;
        if (r < 0 || doc.at(r).depth != place.depth - 1 || doc.at(r).kind != BlockKind::ListItem
            || (moving && r >= movedFirst && r < movedEnd))
            return std::nullopt;
        parent = r;
    }
    // The block after the place (the moved ones jumped over): the sibling the blocks go before, or
    // the end of the parent's children — never a deeper block (its parent would change) or the
    // page's properties.
    PlaceTarget target{parent >= 0 ? doc.at(parent).uid : 0, 0};
    const int next = moving && place.gap == movedFirst ? movedEnd : place.gap;
    if (next < doc.count()) {
        const BlockEntry &e = doc.at(next);
        if (e.depth > place.depth || e.kind == BlockKind::Frontmatter)
            return std::nullopt;
        if (e.depth == place.depth && e.parent == parent)
            target.before = e.uid;
    }
    return target;
}

std::optional<QList<MoveStep>> planBlockMove(const BlockDocument &doc, int first, int last, BlockPlace place)
{
    const std::optional<Moving> m = movingTops(doc, first, last);
    if (!m)
        return std::nullopt;
    const int start = rowOf(doc, m->first);
    const int end = doc.subtreeRange(rowOf(doc, m->last)).last() + 1;
    // (A paragraph, a heading, a table… stays at the top level.)
    for (int r = start; r < end && place.depth > 0; r = doc.subtreeRange(r).last() + 1) {
        if (doc.at(r).kind != BlockKind::ListItem)
            return std::nullopt;
    }
    const std::optional<PlaceTarget> target = placeTarget(doc, place, start, end);
    if (!target)
        return std::nullopt;
    BlockDocument d = doc;
    QList<MoveStep> steps;
    if (!reachParent(d, *m, target->parent, steps))
        return std::nullopt;
    const QList<int> siblings = siblingsOf(d, rowOf(d, m->first));
    const qsizetype gap = target->before ? siblings.indexOf(rowOf(d, target->before)) : siblings.size();
    if (gap < 0 || !slideTo(d, *m, gap, steps) || steps.isEmpty() || d.at(rowOf(d, m->first)).depth != place.depth)
        return std::nullopt;
    return steps;
}

EditResult applyMoveStep(BlockDocument &doc, quint64 firstTop, quint64 lastTop, MoveStep s)
{
    const int a = rowOf(doc, firstTop);
    const int b = rowOf(doc, lastTop);
    if (a < 0 || b < a)
        return {};
    switch (s) {
    case MoveStep::Up:
        return doc.moveRangeUp(a, b);
    case MoveStep::Down:
        return doc.moveRangeDown(a, b);
    case MoveStep::Indent:
        return doc.indentRange(a, b);
    case MoveStep::Outdent:
        return doc.outdentRange(a, b);
    }
    return {};
}

} // namespace brulion::editor
