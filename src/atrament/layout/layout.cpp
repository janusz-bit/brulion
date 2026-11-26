// Layout (docs/ATRAMENT.md D9): the engine — the tree in flat vectors, the per-node cache, leaves
// and their measure functions. The flex algorithm is in flexbox.cpp.

#include "layout/layout.h"

#include "layout/engine.h"

namespace brulion::atrament {

using namespace layout_detail;

namespace {

bool isPercent(Length l)
{
    return l.unit == Length::Unit::Percent;
}

bool usesOwnPercentages(const LayoutStyle &s)
{
    return isPercent(s.width) || isPercent(s.height) || isPercent(s.minWidth) || isPercent(s.minHeight)
           || isPercent(s.maxWidth) || isPercent(s.maxHeight) || isPercent(s.margin.top) || isPercent(s.margin.right)
           || isPercent(s.margin.bottom) || isPercent(s.margin.left);
}

float encode(OptFloat v)
{
    return v ? *v : -1.0f;
}

bool knownMatches(OptFloat query, float entryKnown, float entryResult)
{
    if (!query)
        return entryKnown < 0;
    // A size computed without this dimension known, which came out at the asked value, is the
    // same size (Taffy's rule: it keeps nested measuring from repeating itself).
    return (entryKnown >= 0 && roughlyEqual(*query, entryKnown)) || roughlyEqual(*query, entryResult);
}

bool sizedByStyle(const LayoutStyle &s)
{
    return !s.width.isAuto() || !s.height.isAuto() || !s.minWidth.isAuto() || !s.minHeight.isAuto()
           || !s.maxWidth.isAuto() || !s.maxHeight.isAuto() || s.aspectRatio.has_value();
}

bool availableMatches(AvailableSpace query, AvailableSpace::Kind kind, float value, float result)
{
    if (query.kind == kind && (kind != AvailableSpace::Kind::Definite || roughlyEqual(query.value, value)))
        return true;
    // Sizes follow fit-content (layout.h, MeasureFunction): a size that fit within the space it
    // was computed in is the same in a smaller space it still fits within, and a max-content size
    // is the same in any definite space it fits within.
    if (query.kind != AvailableSpace::Kind::Definite || kind == AvailableSpace::Kind::MinContent)
        return false;
    const float tolerance = 1e-4f * std::max(1.0f, std::abs(query.value));
    return result <= query.value + tolerance
           && (kind == AvailableSpace::Kind::MaxContent || (result <= value + tolerance && query.value <= value));
}

bool parentMatches(OptFloat query, float entry)
{
    return query ? entry >= 0 && roughlyEqual(*query, entry) : entry < 0;
}

bool matches(const CacheEntry &e, const LayoutInput &in, const Node &node)
{
    return (e.sizing == in.sizing || !node.sizedByStyle) && knownMatches(in.known.width, e.knownWidth, e.result.width)
           && knownMatches(in.known.height, e.knownHeight, e.result.height)
           && (in.known.width
               || availableMatches(in.availableWidth, e.availableWidthKind, e.availableWidth, e.result.width))
           && (in.known.height
               || availableMatches(in.availableHeight, e.availableHeightKind, e.availableHeight, e.result.height))
           && (!node.ownPercentages
               || (parentMatches(in.parent.width, e.parentWidth) && parentMatches(in.parent.height, e.parentHeight)));
}

// Taffy's slot choice: by the dimensions known, then by which available spaces are min-content.
int sizeSlot(const LayoutInput &in)
{
    const bool width = in.known.width.has_value();
    const bool height = in.known.height.has_value();
    const bool minWidth = in.availableWidth.kind == AvailableSpace::Kind::MinContent;
    const bool minHeight = in.availableHeight.kind == AvailableSpace::Kind::MinContent;
    if (width && height)
        return 0;
    if (width)
        return minHeight ? 2 : 1;
    if (height)
        return minWidth ? 4 : 3;
    return 5 + (minHeight ? 1 : 0) + (minWidth ? 2 : 0);
}

Size sanitized(Size s)
{
    return {nonNegative(s.width), nonNegative(s.height)};
}

// A leaf's content is its padding box (a measure function's result already fills the box).
void setLeafLayout(Node &node, Size size)
{
    const Edges border = nonNegative(node.style.border);
    node.layout.rect.size = size;
    node.layout.contentSize = {std::max(0.0f, size.width - border.horizontal()),
                               std::max(0.0f, size.height - border.vertical())};
}

} // namespace

namespace layout_detail {

OwnBox Engine::resolveOwnBox(NodeId id, const LayoutInput &in) const
{
    const LayoutStyle &s = nodes[id].style;
    OwnBox b;
    b.padding = nonNegative(s.padding);
    b.border = nonNegative(s.border);
    b.pbWidth = b.padding.horizontal() + b.border.horizontal();
    b.pbHeight = b.padding.vertical() + b.border.vertical();
    if (id != root) {
        // CSS: percentages of margins in both axes are of the containing block's width.
        const OptFloat base = in.parent.width;
        b.margin = {resolveEdge(s.margin.top, base).value_or(0), resolveEdge(s.margin.right, base).value_or(0),
                    resolveEdge(s.margin.bottom, base).value_or(0), resolveEdge(s.margin.left, base).value_or(0)};
    }

    OptFloat styleWidth;
    OptFloat styleHeight;
    const bool inherent = in.sizing == SizingMode::InherentSize;
    // The ratio transfers a known dimension in both modes: the content size of a box with an
    // aspect ratio and a definite size in the other axis is the transferred size (CSS Sizing 4).
    const OptFloat ratio = aspectRatio(s);
    if (inherent) {
        b.minWidth = resolveMinSize(s.minWidth, in.parent.width);
        b.maxWidth = resolveSize(s.maxWidth, in.parent.width);
        b.minHeight = resolveMinSize(s.minHeight, in.parent.height);
        b.maxHeight = resolveSize(s.maxHeight, in.parent.height);
        styleWidth = resolveSize(s.width, in.parent.width);
        styleHeight = resolveSize(s.height, in.parent.height);
        if (ratio) { // from the used size of the other axis (CSS Sizing 4: after its min/max)
            if (styleWidth && !styleHeight)
                styleHeight = clampLength(clampOpt(*styleWidth, b.minWidth, b.maxWidth) / *ratio);
            else if (styleHeight && !styleWidth)
                styleWidth = clampLength(clampOpt(*styleHeight, b.minHeight, b.maxHeight) * *ratio);
        }
        const auto settle = [](OptFloat size, OptFloat lo, OptFloat hi) -> OptFloat {
            if (size)
                return clampOpt(*size, lo, hi);
            if (lo && hi && *hi <= *lo) // max ≤ min fixes the size
                return lo;
            return std::nullopt;
        };
        styleWidth = settle(styleWidth, b.minWidth, b.maxWidth);
        styleHeight = settle(styleHeight, b.minHeight, b.maxHeight);
    }
    b.knownWidth = in.known.width ? in.known.width : styleWidth;
    b.knownHeight = in.known.height ? in.known.height : styleHeight;
    if (ratio) {
        if (b.knownWidth && !b.knownHeight)
            b.knownHeight = clampOpt(clampLength(*b.knownWidth / *ratio), b.minHeight, b.maxHeight);
        else if (b.knownHeight && !b.knownWidth)
            b.knownWidth = clampOpt(clampLength(*b.knownHeight * *ratio), b.minWidth, b.maxWidth);
    }
    // border-box sizing: the content box is never negative.
    if (b.knownWidth)
        b.knownWidth = std::max(*b.knownWidth, b.pbWidth);
    if (b.knownHeight)
        b.knownHeight = std::max(*b.knownHeight, b.pbHeight);
    return b;
}

void Engine::hide(NodeId id)
{
    Node &node = nodes[id];
    node.layout = {};
    for (const NodeId child : childrenOf(node))
        hide(child);
}

const CacheEntry *Engine::lookup(NodeId id, const LayoutInput &in, bool fullLayout)
{
    if (nodes[id].cache == kNoCache)
        return nullptr;
    NodeCache &cache = caches[nodes[id].cache];
    if (cache.generation != generation) {
        cache.generation = generation;
        cache.valid = 0;
        return nullptr;
    }
    const Node &node = nodes[id];
    if (fullLayout) {
        // Only a full layout with the same inputs has positioned the children already.
        const CacheEntry &e = cache.entries[NodeCache::kLayoutSlot];
        const bool valid = (cache.valid >> NodeCache::kLayoutSlot) & 1;
        return valid && matches(e, in, node) ? &e : nullptr;
    }
    for (int i = 0; i <= NodeCache::kLayoutSlot; ++i) {
        if (((cache.valid >> i) & 1) && matches(cache.entries[std::size_t(i)], in, node))
            return &cache.entries[std::size_t(i)];
    }
    return nullptr;
}

void Engine::store(NodeId id, const LayoutInput &in, bool fullLayout, Size size)
{
    if (nodes[id].cache == kNoCache)
        return;
    NodeCache &cache = caches[nodes[id].cache];
    const int slot = fullLayout ? NodeCache::kLayoutSlot : sizeSlot(in);
    CacheEntry &e = cache.entries[std::size_t(slot)];
    e.knownWidth = encode(in.known.width);
    e.knownHeight = encode(in.known.height);
    e.availableWidth = in.availableWidth.value;
    e.availableHeight = in.availableHeight.value;
    e.availableWidthKind = in.availableWidth.kind;
    e.availableHeightKind = in.availableHeight.kind;
    e.parentWidth = encode(in.parent.width);
    e.parentHeight = encode(in.parent.height);
    e.result = size;
    e.sizing = in.sizing;
    cache.valid = std::uint16_t(cache.valid | (1u << slot));
}

Size Engine::computeNode(NodeId id, const LayoutInput &in)
{
    Node &node = nodes[id];
    if (node.style.display == Display::None) {
        if (in.mode == RunMode::PerformLayout)
            hide(id);
        return {};
    }
    const bool leaf = node.childCount == 0;
    const bool fullLayout = in.mode == RunMode::PerformLayout && !leaf;
    if (const CacheEntry *hit = lookup(id, in, fullLayout)) {
        if (in.mode == RunMode::PerformLayout && leaf)
            setLeafLayout(node, hit->result);
        return hit->result;
    }
    const Size size = sanitized(leaf ? computeLeaf(id, in) : computeFlex(id, in));
    store(id, in, fullLayout, size);
    return size;
}

Size Engine::computeLeaf(NodeId id, const LayoutInput &in)
{
    Node &node = nodes[id];
    const OwnBox b = resolveOwnBox(id, in);
    Size size;
    if (b.knownWidth && b.knownHeight) {
        size = {*b.knownWidth, *b.knownHeight};
    } else if (node.measure) {
        // The measure function sees the content box: padding and border are added back here.
        const AvailableSpace width = innerAvailable(b.knownWidth, in.availableWidth, b.margin.horizontal(), b.minWidth,
                                                    b.maxWidth, b.pbWidth);
        const AvailableSpace height = innerAvailable(b.knownHeight, in.availableHeight, b.margin.vertical(),
                                                     b.minHeight, b.maxHeight, b.pbHeight);
        const OptFloat knownWidth = b.knownWidth ? OptFloat(*b.knownWidth - b.pbWidth) : std::nullopt;
        const OptFloat knownHeight = b.knownHeight ? OptFloat(*b.knownHeight - b.pbHeight) : std::nullopt;
        ++measureCalls;
        const Size measured = node.measure(knownWidth, knownHeight, width, height);
        size.width = b.knownWidth ? *b.knownWidth
                                  : std::max(b.pbWidth,
                                             clampOpt(nonNegative(measured.width) + b.pbWidth, b.minWidth, b.maxWidth));
        size.height = b.knownHeight ? *b.knownHeight
                                    : std::max(b.pbHeight, clampOpt(nonNegative(measured.height) + b.pbHeight,
                                                                    b.minHeight, b.maxHeight));
    } else {
        size.width = b.knownWidth ? *b.knownWidth : std::max(b.pbWidth, clampOpt(b.pbWidth, b.minWidth, b.maxWidth));
        size.height =
                b.knownHeight ? *b.knownHeight : std::max(b.pbHeight, clampOpt(b.pbHeight, b.minHeight, b.maxHeight));
    }
    size = sanitized(size);
    if (in.mode == RunMode::PerformLayout)
        setLeafLayout(node, size);
    return size;
}

} // namespace layout_detail

struct LayoutEngine::Impl : Engine { };

LayoutEngine::LayoutEngine() : d(std::make_unique<Impl>()) { }

LayoutEngine::~LayoutEngine() = default;

void LayoutEngine::clear()
{
    d->nodes.clear();
    d->childIds.clear();
    d->cacheCount = 0;
    d->root = kNoNode;
    d->measureCalls = 0;
}

NodeId LayoutEngine::newNode(const LayoutStyle &style, std::span<const NodeId> children)
{
    const auto id = NodeId(d->nodes.size());
    Node &node = d->nodes.emplace_back();
    node.style = style;
    node.ownPercentages = usesOwnPercentages(style);
    node.sizedByStyle = sizedByStyle(style);
    node.firstChild = std::uint32_t(d->childIds.size());
    for (const NodeId child : children) {
        // Children exist before their parent (so the tree has no cycles) and have one parent.
        if (child >= id || d->nodes[child].parent != kNoNode)
            continue;
        d->nodes[child].parent = id;
        d->childIds.push_back(child);
    }
    node.childCount = std::uint32_t(d->childIds.size()) - node.firstChild;
    if (node.childCount > 0)
        node.cache = d->cacheCount++;
    return id;
}

NodeId LayoutEngine::newLeaf(const LayoutStyle &style, MeasureFunction measure)
{
    const NodeId id = newNode(style, {});
    Node &node = d->nodes[id];
    if (measure) {
        node.measure = std::move(measure);
        node.cache = d->cacheCount++;
    }
    return id;
}

void LayoutEngine::compute(NodeId root, Size available)
{
    compute(root, AvailableSpace::definite(available.width), AvailableSpace::definite(available.height));
}

void LayoutEngine::compute(NodeId root, AvailableSpace width, AvailableSpace height)
{
    d->measureCalls = 0;
    if (root >= d->nodes.size())
        return;
    if (d->caches.size() < d->cacheCount)
        d->caches.resize(d->cacheCount);
    if (++d->generation == 0) { // wrapped: no entry may look current
        for (NodeCache &cache : d->caches)
            cache.generation = 0;
        d->generation = 1;
    }
    d->root = root;
    if (width.isDefinite())
        width.value = nonNegative(width.value);
    if (height.isDefinite())
        height.value = nonNegative(height.value);

    LayoutInput in;
    in.parent = {width.isDefinite() ? OptFloat(width.value) : std::nullopt,
                 height.isDefinite() ? OptFloat(height.value) : std::nullopt};
    in.availableWidth = width;
    in.availableHeight = height;
    // As a parent does with its items: the size first, then the layout at that size (so an
    // auto-sized root's percentages and stretched items resolve against the size it got).
    const Size size = d->computeNode(root, in);
    in.known = {size.width, size.height};
    in.mode = RunMode::PerformLayout;
    d->computeNode(root, in);
    d->nodes[root].layout.rect.origin = {};
}

const NodeLayout &LayoutEngine::layout(NodeId node) const
{
    static const NodeLayout none;
    return node < d->nodes.size() ? d->nodes[node].layout : none;
}

Rect LayoutEngine::absolute(NodeId node) const
{
    if (node >= d->nodes.size())
        return {};
    Rect r = d->nodes[node].layout.rect;
    for (NodeId at = node; at != d->root;) {
        const NodeId up = d->nodes[at].parent;
        if (up == kNoNode)
            break;
        r.origin += d->nodes[up].layout.rect.origin;
        at = up;
    }
    return r;
}

NodeId LayoutEngine::parent(NodeId node) const
{
    return node < d->nodes.size() ? d->nodes[node].parent : kNoNode;
}

const LayoutStyle &LayoutEngine::style(NodeId node) const
{
    static const LayoutStyle none;
    return node < d->nodes.size() ? d->nodes[node].style : none;
}

std::size_t LayoutEngine::nodeCount() const
{
    return d->nodes.size();
}

std::size_t LayoutEngine::measureCalls() const
{
    return d->measureCalls;
}

} // namespace brulion::atrament
