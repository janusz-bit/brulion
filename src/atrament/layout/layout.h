#pragma once

// Layout (docs/ATRAMENT.md D9): Atrament's own flexbox — the subset of CSS Flexible Box Layout
// (https://www.w3.org/TR/css-flexbox-1/) Brulion's UI uses — over a tree of nodes rebuilt for
// every frame. Lengths are logical pixels.
//
// Supported, with CSS's meaning:
//   display            flex | none (a `none` node and its subtree take no space and get a zero rect)
//   position           relative | absolute (out of flow; placed by `inset` against the parent's
//                      padding box; an auto inset falls back to the alignment's static position)
//   flex-direction     row | column | row-reverse | column-reverse
//   flex-wrap          nowrap | wrap
//   justify-content    flex-start | flex-end | center | space-between | space-around | space-evenly
//   align-items, align-self   stretch | flex-start | flex-end | center | baseline (as flex-start)
//   align-content      stretch | flex-start | flex-end | center | space-between | space-around
//   flex-grow, flex-shrink, flex-basis (auto | px | %)
//   width, height, min-*, max-* (auto | px | %), aspect-ratio
//   margin (px | % | auto: auto margins absorb free space), padding and border (px)
//   gap (row and column)
//   overflow           visible | hidden | scroll: hidden and scroll make an item's automatic minimum
//                      size 0 (CSS 4.5), so a scrolled list never grows its container
//   box-sizing         border-box (always: width and height include padding and border)
//
// Leaves may have a measure function (text, pictures): it is asked for a size given the known
// dimensions and the available space, as Taffy and Yoga do. Results are cached for the length of
// one compute().
//
// Percentages resolve against the containing block's size when that is definite, else they act
// as auto. A node's result is its border box relative to its parent's border box; `absolute()`
// gives positions relative to the root.
//
// A container of auto size is sized by its content: a row as fit-content (its max-content width,
// at most the available width, at least its min-content width), a column as high as its content
// (as a block); the cross size is that of its lines. The root's margins are ignored (it is
// placed at (0, 0)); give it a size (100 %) to fill the window. Not supported: order,
// wrap-reverse, baselines, writing modes, `flex-basis: content` (an indefinite percentage acts as
// it), the line-wide max-content flex fraction of 9.9.1 (as in browsers).

#include "base/geometry.h"

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace brulion::atrament {

struct Length {
    enum class Unit : std::uint8_t { Auto, Px, Percent };
    Unit unit = Unit::Auto;
    float value = 0;

    static constexpr Length autoLength() { return {}; }
    static constexpr Length px(float v) { return { Unit::Px, v }; }
    static constexpr Length percent(float v) { return { Unit::Percent, v }; } // 0–100
    constexpr bool isAuto() const { return unit == Unit::Auto; }
    constexpr bool operator==(const Length &) const = default;
};

struct LengthEdges {
    Length top = Length::px(0);
    Length right = Length::px(0);
    Length bottom = Length::px(0);
    Length left = Length::px(0);

    static constexpr LengthEdges all(Length l) { return { l, l, l, l }; }
    static constexpr LengthEdges autoEdges()
    {
        return { Length::autoLength(), Length::autoLength(), Length::autoLength(), Length::autoLength() };
    }
    constexpr bool operator==(const LengthEdges &) const = default;
};

enum class Display : std::uint8_t { Flex, None };
enum class PositionType : std::uint8_t { Relative, Absolute };
enum class FlexDirection : std::uint8_t { Row, Column, RowReverse, ColumnReverse };
enum class FlexWrap : std::uint8_t { NoWrap, Wrap };
enum class JustifyContent : std::uint8_t { FlexStart, FlexEnd, Center, SpaceBetween, SpaceAround, SpaceEvenly };
// `Auto` is only meaningful for align-self (= the parent's align-items).
enum class AlignItems : std::uint8_t { Auto, Stretch, FlexStart, FlexEnd, Center, Baseline };
enum class AlignContent : std::uint8_t { Stretch, FlexStart, FlexEnd, Center, SpaceBetween, SpaceAround };
enum class Overflow : std::uint8_t { Visible, Hidden, Scroll };

struct LayoutStyle {
    Display display = Display::Flex;
    PositionType position = PositionType::Relative;
    FlexDirection flexDirection = FlexDirection::Row;
    FlexWrap flexWrap = FlexWrap::NoWrap;
    JustifyContent justifyContent = JustifyContent::FlexStart;
    AlignItems alignItems = AlignItems::Stretch;
    AlignItems alignSelf = AlignItems::Auto;
    AlignContent alignContent = AlignContent::Stretch;
    float flexGrow = 0;
    float flexShrink = 1;
    Length flexBasis = Length::autoLength();
    Length width = Length::autoLength();
    Length height = Length::autoLength();
    Length minWidth = Length::autoLength(); // auto: CSS's automatic minimum size for flex items
    Length minHeight = Length::autoLength();
    Length maxWidth = Length::autoLength(); // auto: none
    Length maxHeight = Length::autoLength();
    std::optional<float> aspectRatio; // width / height
    LengthEdges margin;
    Edges padding;
    Edges border;
    float rowGap = 0; // between lines of a wrapping row / items of a column
    float columnGap = 0; // between items of a row / lines of a wrapping column
    LengthEdges inset = LengthEdges::autoEdges(); // for absolute position
    Overflow overflowX = Overflow::Visible;
    Overflow overflowY = Overflow::Visible;
};

// The space a node may take along one axis.
struct AvailableSpace {
    enum class Kind : std::uint8_t { Definite, MinContent, MaxContent };
    Kind kind = Kind::MaxContent;
    float value = 0; // for Definite

    static constexpr AvailableSpace definite(float v) { return { Kind::Definite, v }; }
    static constexpr AvailableSpace minContent() { return { Kind::MinContent, 0 }; }
    static constexpr AvailableSpace maxContent() { return { Kind::MaxContent, 0 }; }
    constexpr bool isDefinite() const { return kind == Kind::Definite; }
    constexpr bool operator==(const AvailableSpace &) const = default;
};

// A leaf's size: `knownWidth` / `knownHeight` are set when the layout already decided that
// dimension (the result must then use it); the available space bounds the rest. A text leaf
// wraps at a definite available width, takes its longest word for MinContent and its unwrapped
// width for MaxContent. All sizes are of the leaf's content box (inside its padding and border).
// An intrinsic width (a row's item measured for its base size or its minimum) is asked for with a
// max-content available height: intrinsic widths do not depend on the height there is.
// Sizes are taken to follow fit-content: one that fits within the available space it was
// measured in is reused for any smaller space it still fits within (and a max-content size for
// any definite space it fits within), so a measure function is not called again for those.
using MeasureFunction = std::function<Size(std::optional<float> knownWidth, std::optional<float> knownHeight,
        AvailableSpace availableWidth, AvailableSpace availableHeight)>;

using NodeId = std::uint32_t;
constexpr NodeId kNoNode = std::numeric_limits<NodeId>::max();

struct NodeLayout {
    Rect rect; // border box, relative to the parent's border box
    // The size of the content (children's margin boxes and padding), for scroll containers.
    Size contentSize;
};

class LayoutEngine {
public:
    LayoutEngine();
    ~LayoutEngine();
    LayoutEngine(const LayoutEngine &) = delete;
    LayoutEngine &operator=(const LayoutEngine &) = delete;

    // Forgets every node (keeps the memory); ids start again from 0.
    void clear();

    NodeId newNode(const LayoutStyle &style, std::span<const NodeId> children);
    NodeId newLeaf(const LayoutStyle &style, MeasureFunction measure = {});

    // Lays out the tree under `root` in `available` space (a definite width and height for a
    // window). The root is placed at (0, 0).
    void compute(NodeId root, Size available);
    // Same with each axis's available space given explicitly (e.g. max-content height for a list
    // that measures its rows).
    void compute(NodeId root, AvailableSpace width, AvailableSpace height);

    const NodeLayout &layout(NodeId node) const;
    // The border box relative to the root's origin.
    Rect absolute(NodeId node) const;
    NodeId parent(NodeId node) const;
    const LayoutStyle &style(NodeId node) const;
    std::size_t nodeCount() const;

    // How many times measure functions ran during the last compute() (caching keeps this near
    // the number of leaves; tests use it).
    std::size_t measureCalls() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace brulion::atrament
