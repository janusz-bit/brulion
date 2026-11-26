#pragma once

// Private to src/atrament/layout: the engine's storage (flat vectors of nodes and child ids), the
// per-node cache of sizes, and the inputs of laying out one node in the shape Taffy gives them —
// the sizes the parent already decided, the containing block's size (for percentages), the
// available space, and whether the children are positioned or the node is only measured.
// layout.cpp holds the engine, the leaves and the cache; flexbox.cpp the flex algorithm.

#include "layout/layout.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace brulion::atrament::layout_detail {

// Every length is clamped to ±kMaxLength, so sums and products of them stay finite in float.
inline constexpr float kMaxLength = 1e9f;

using OptFloat = std::optional<float>;

struct OptSize {
    OptFloat width;
    OptFloat height;
};

enum class RunMode : std::uint8_t {
    ComputeSize,   // only the node's size is wanted (a measurement while its parent sizes its items)
    PerformLayout, // the final size: the node's children are positioned too
};

enum class SizingMode : std::uint8_t {
    InherentSize, // the node's own width, height, min-*, max-* and aspect ratio apply
    ContentSize,  // only its content counts: a flex item's flex base size and its automatic minimum
                  // size ignore the item's own sizes (CSS Flexbox 9.2.3 E, 4.5)
};

struct LayoutInput {
    OptSize known;                 // border-box sizes the parent already decided
    OptSize parent;                // the containing block's size, for percentages (nullopt: indefinite)
    AvailableSpace availableWidth; // for the node's margin box
    AvailableSpace availableHeight;
    RunMode mode = RunMode::ComputeSize;
    SizingMode sizing = SizingMode::InherentSize;
};

inline constexpr std::uint32_t kNoCache = std::numeric_limits<std::uint32_t>::max();

struct Node {
    LayoutStyle style;
    MeasureFunction measure;
    NodeLayout layout;
    NodeId parent = kNoNode;
    std::uint32_t firstChild = 0; // into Engine::childIds
    std::uint32_t childCount = 0;
    // Into Engine::caches: containers and measured leaves only (any other leaf is cheaper to
    // compute than to look up).
    std::uint32_t cache = kNoCache;
    // Its own sizes or margins use percentages, so the containing block's size is part of its
    // cache key (its children resolve theirs against its own size, which the key holds already).
    bool ownPercentages = false;
    // It has a width, height, min-*, max-* or aspect ratio: its content size (SizingMode) can
    // differ from its inherent size, so the sizing mode is part of its cache key.
    bool sizedByStyle = false;
};

// One cached size of a node for one set of inputs (-1: not known / indefinite).
struct CacheEntry {
    float knownWidth;
    float knownHeight;
    float availableWidth;
    float availableHeight;
    float parentWidth;
    float parentHeight;
    Size result;
    AvailableSpace::Kind availableWidthKind;
    AvailableSpace::Kind availableHeightKind;
    SizingMode sizing;
};

// Taffy's cache: nine slots for sizes, chosen by which dimensions are known and which available
// spaces are min-content (so the measurements of one flex pass do not evict each other), and one
// for the last full layout. Valid only during the compute() that wrote it (`generation`).
struct NodeCache {
    static constexpr int kLayoutSlot = 9; // 0–8: sizes

    NodeCache() { } // the entries stay uninitialised: only valid ones are read
    std::uint32_t generation = 0;
    std::uint16_t valid = 0; // a bit per entry
    std::array<CacheEntry, 10> entries;
};

// The node's own box for one layout input.
struct OwnBox {
    Edges padding;
    Edges border;
    Edges margin;      // auto margins as 0; the root's are ignored
    float pbWidth = 0; // padding + border
    float pbHeight = 0;
    // Its min and max sizes (InherentSize only), border box; min auto is nullopt.
    OptFloat minWidth;
    OptFloat maxWidth;
    OptFloat minHeight;
    OptFloat maxHeight;
    // Its border-box sizes decided before looking at the content: by the parent or by its style.
    OptFloat knownWidth;
    OptFloat knownHeight;
};

// A flex item while its container is laid out (main/cross mapped from the direction; margins and
// padding on the physical start and end sides of each axis).
struct FlexItem {
    NodeId node = kNoNode;
    AlignItems align = AlignItems::Stretch;
    bool scrollContainer = false;
    bool minMainAuto = false;
    bool minResolved = false;
    bool basisFromContent = false;
    bool frozen = false;
    bool autoMainStart = false;
    bool autoMainEnd = false;
    bool autoCrossStart = false;
    bool autoCrossEnd = false;
    bool mainPercentages = false; // a row's item with percentages along the main axis
    AvailableSpace::Kind basisKind = AvailableSpace::Kind::MaxContent;
    float grow = 0;
    float shrink = 1;
    OptFloat sizeMain; // its width/height, border box
    OptFloat sizeCross;
    OptFloat minMain; // nullopt: auto
    OptFloat maxMain;
    OptFloat minCross;
    OptFloat maxCross;
    OptFloat aspect;
    OptFloat contentKnownCross; // the cross size its content is measured with (9.2.3 E)
    float marginMainStart = 0;
    float marginMainEnd = 0;
    float marginCrossStart = 0;
    float marginCrossEnd = 0;
    float pbMain = 0;
    float pbCross = 0;
    float basis = 0;       // flex base size, border box
    float innerBasis = 0;  // content box (what the shrink factor scales, 9.7)
    float resolvedMin = 0; // the used min main size (minResolved)
    float hypotheticalMain = 0;
    float targetMain = 0;
    float violation = 0;
    float hypotheticalCross = 0;
    float targetCross = 0;
    float offsetMain = 0; // of the border box within the content box
    float offsetCross = 0;
    float usedMarginMainStart = 0;
    float usedMarginMainEnd = 0;
    float usedMarginCrossStart = 0;
    float usedMarginCrossEnd = 0;
};

struct FlexLine {
    std::uint32_t begin = 0; // items [begin, end)
    std::uint32_t end = 0;
    float crossSize = 0;
    float crossOffset = 0;
};

// The working memory of one level of nested flex layouts, reused frame after frame.
struct Scratch {
    std::vector<FlexItem> items;
    std::vector<FlexLine> lines;
    std::vector<NodeId> absolute;
};

class Engine {
public:
    Size computeNode(NodeId id, const LayoutInput &in);
    OwnBox resolveOwnBox(NodeId id, const LayoutInput &in) const;
    // Zero rects for a `display: none` node and its subtree.
    void hide(NodeId id);
    std::span<const NodeId> childrenOf(const Node &node) const
    {
        return std::span<const NodeId>(childIds).subspan(node.firstChild, node.childCount);
    }

    std::vector<Node> nodes;
    std::vector<NodeId> childIds;
    std::vector<NodeCache> caches; // never shrinks: generations invalidate it
    std::uint32_t cacheCount = 0;  // in use since clear()
    std::deque<Scratch> scratch;   // one per depth of nested flex layouts (references stay valid)
    std::uint32_t depth = 0;
    std::uint32_t generation = 0;
    std::size_t measureCalls = 0;
    NodeId root = kNoNode;

private:
    Size computeLeaf(NodeId id, const LayoutInput &in);
    Size computeFlex(NodeId id, const LayoutInput &in); // flexbox.cpp
    const CacheEntry *lookup(NodeId id, const LayoutInput &in, bool fullLayout);
    void store(NodeId id, const LayoutInput &in, bool fullLayout, Size size);
};

// --- Lengths ---------------------------------------------------------------------------------

inline bool roughlyEqual(float a, float b)
{
    return std::abs(a - b) <= 1e-4f * std::max(1.0f, std::abs(a));
}

// NaN → 0; ±inf and beyond → ±kMaxLength.
inline float clampLength(float v)
{
    if (std::isnan(v))
        return 0;
    return std::clamp(v, -kMaxLength, kMaxLength);
}

inline float nonNegative(float v)
{
    return v > 0 ? std::min(v, kMaxLength) : 0.0f;
}

inline Edges nonNegative(const Edges &e)
{
    return {nonNegative(e.top), nonNegative(e.right), nonNegative(e.bottom), nonNegative(e.left)};
}

// width, height, max-*, flex-basis: a negative value is invalid in CSS (auto / none); a
// percentage of an indefinite size behaves as auto.
inline OptFloat resolveSize(Length l, OptFloat base)
{
    if (!(l.value >= 0))
        return std::nullopt;
    switch (l.unit) {
    case Length::Unit::Auto: return std::nullopt;
    case Length::Unit::Px: return std::min(l.value, kMaxLength);
    case Length::Unit::Percent:
        if (!base)
            return std::nullopt;
        return std::min(*base * (std::min(l.value, 1e6f) / 100.0f), kMaxLength);
    }
    return std::nullopt;
}

// min-width, min-height: auto (and an invalid value) stays nullopt — the automatic minimum; a
// percentage of an indefinite size resolves against zero (CSS Sizing 3, 5.2.1).
inline OptFloat resolveMinSize(Length l, OptFloat base)
{
    if (l.unit == Length::Unit::Percent && !base && l.value >= 0)
        return 0.0f;
    return resolveSize(l, base);
}

// Margins and insets: may be negative; auto stays nullopt; a percentage of an indefinite size is 0.
inline OptFloat resolveEdge(Length l, OptFloat base)
{
    const float v = std::isnan(l.value) ? 0.0f : std::clamp(l.value, -kMaxLength, kMaxLength);
    switch (l.unit) {
    case Length::Unit::Auto: return std::nullopt;
    case Length::Unit::Px: return v;
    case Length::Unit::Percent:
        if (!base)
            return 0.0f;
        return clampLength(*base * (std::clamp(v, -1e6f, 1e6f) / 100.0f));
    }
    return std::nullopt;
}

// CSS's clamp: max first, then min, so min wins when max < min.
inline float clampOpt(float v, OptFloat lo, OptFloat hi)
{
    if (hi)
        v = std::min(v, *hi);
    if (lo)
        v = std::max(v, *lo);
    return v;
}

inline OptFloat minus(OptFloat v, float d)
{
    return v ? OptFloat(*v - d) : std::nullopt;
}

inline OptFloat aspectRatio(const LayoutStyle &s)
{
    if (!s.aspectRatio || !(*s.aspectRatio > 0) || !std::isfinite(*s.aspectRatio))
        return std::nullopt;
    return std::clamp(*s.aspectRatio, 1e-6f, 1e6f);
}

// A negative (or NaN) flex factor is invalid in CSS: the initial value applies.
inline float flexFactor(float f, float initial)
{
    return f >= 0 ? std::min(f, std::numeric_limits<float>::max()) : initial;
}

// The space for a node's children along one axis: its inner (content-box) size when that is
// known, else what it was given minus its margins, clamped by its min/max, minus padding and border.
inline AvailableSpace innerAvailable(OptFloat known, AvailableSpace available, float marginSum, OptFloat minSize,
                                     OptFloat maxSize, float paddingBorder)
{
    if (known)
        return AvailableSpace::definite(std::max(0.0f, *known - paddingBorder));
    if (!available.isDefinite())
        return available;
    const float outer = clampOpt(clampLength(available.value) - marginSum, minSize, maxSize);
    return AvailableSpace::definite(std::max(0.0f, outer - paddingBorder));
}

} // namespace brulion::atrament::layout_detail
