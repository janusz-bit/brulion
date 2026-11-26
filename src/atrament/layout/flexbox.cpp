// Layout (docs/ATRAMENT.md D9): the CSS Flexible Box Layout algorithm
// (https://www.w3.org/TR/css-flexbox-1/ §9) for the subset layout.h lists, structured as Taffy's
// (crates/taffy/src/compute/flexbox.rs) — every container may be asked for its size many times
// (min-content, max-content, with one dimension known) before its final layout; the engine's
// cache keeps that from repeating work. Section numbers in the comments are the spec's.

#include "layout/engine.h"

namespace brulion::atrament::layout_detail {

namespace {

constexpr float kInfinity = std::numeric_limits<float>::infinity();
// Items that fit a line exactly must not be pushed to the next one by rounding.
constexpr float kLineEpsilon = 1e-3f;

bool isRow(FlexDirection d)
{
    return d == FlexDirection::Row || d == FlexDirection::RowReverse;
}

bool isReverse(FlexDirection d)
{
    return d == FlexDirection::RowReverse || d == FlexDirection::ColumnReverse;
}

AlignItems resolvedAlign(AlignItems self, AlignItems items)
{
    AlignItems a = self == AlignItems::Auto ? items : self;
    if (a == AlignItems::Auto)
        a = AlignItems::Stretch;
    if (a == AlignItems::Baseline) // baselines are not supported: as flex-start (layout.h)
        a = AlignItems::FlexStart;
    return a;
}

enum class Place : std::uint8_t { Start, Center, End };

float placed(Place place, float contentStart, float contentEnd, float size, float marginStart, float marginEnd)
{
    switch (place) {
    case Place::Start: return contentStart + marginStart;
    case Place::End: return contentEnd - marginEnd - size;
    case Place::Center:
        return contentStart + marginStart + (contentEnd - contentStart - size - marginStart - marginEnd) / 2;
    }
    return contentStart;
}

class FlexAlgorithm {
public:
    FlexAlgorithm(Engine &engine, NodeId id, const LayoutInput &in, Scratch &scratch);
    Size run();

private:
    float mainOf(Size s) const { return row_ ? s.width : s.height; }
    float crossOf(Size s) const { return row_ ? s.height : s.width; }
    Size sizeOf(float main, float cross) const { return row_ ? Size{main, cross} : Size{cross, main}; }
    OptSize optSizeOf(OptFloat main, OptFloat cross) const
    {
        return row_ ? OptSize{main, cross} : OptSize{cross, main};
    }
    OptSize innerSize() const { return optSizeOf(innerMain_, innerCross_); }
    std::span<FlexItem> lineItems(const FlexLine &line)
    {
        return std::span<FlexItem>(s_.items).subspan(line.begin, line.end - line.begin);
    }
    static bool stretched(const FlexItem &item)
    {
        return item.align == AlignItems::Stretch && !item.sizeCross && !item.autoCrossStart && !item.autoCrossEnd;
    }

    void collectChildren();
    void initItem(FlexItem &item, NodeId child) const;
    void computeBaseSize(FlexItem &item);
    float measureContentMain(const FlexItem &item, AvailableSpace::Kind kind);
    float minMainSize(FlexItem &item);
    float contribution(FlexItem &item, bool minContent);
    float rowContentMain();
    float clampContainerMain(float innerContent) const;
    void collectLines(float limit);
    float lineOuterHypotheticalMain(const FlexLine &line) const;
    void resolveFlexibleLengths(const FlexLine &line);
    void computeHypotheticalCross(FlexItem &item);
    void alignMain(const FlexLine &line);
    void alignCross(const FlexLine &line, FlexItem &item) const;
    void layoutItem(const FlexItem &item);
    void layoutAbsolute(NodeId child);
    void extendContent(const Rect &rect, const Edges &margin);

    Engine &e_;
    const NodeId id_;
    const LayoutInput &in_;
    Scratch &s_;
    const LayoutStyle &style_;
    const OwnBox box_;
    const bool row_;
    const bool reverse_;
    const bool wrap_;
    float pbMain_ = 0;
    float pbCross_ = 0;
    float pbMainStart_ = 0; // padding + border on the physical start side
    float pbCrossStart_ = 0;
    float gapMain_ = 0;
    float gapCross_ = 0;
    OptFloat minMain_; // the container's own, border box
    OptFloat maxMain_;
    OptFloat minCross_;
    OptFloat maxCross_;
    OptFloat knownMain_;
    OptFloat knownCross_;
    OptFloat innerMain_; // content box, once definite
    OptFloat innerCross_;
    OptSize contentBlock_;     // what the items' percentages resolve against while they are measured
    AvailableSpace availMain_; // for the items
    AvailableSpace availCross_;
    float main_ = 0;
    float cross_ = 0;
    Size contentSize_;
};

FlexAlgorithm::FlexAlgorithm(Engine &engine, NodeId id, const LayoutInput &in, Scratch &scratch)
    : e_(engine)
    , id_(id)
    , in_(in)
    , s_(scratch)
    , style_(engine.nodes[id].style)
    , box_(engine.resolveOwnBox(id, in))
    , row_(isRow(style_.flexDirection))
    , reverse_(isReverse(style_.flexDirection))
    , wrap_(style_.flexWrap == FlexWrap::Wrap)
{
    const Edges &p = box_.padding;
    const Edges &b = box_.border;
    pbMain_ = row_ ? box_.pbWidth : box_.pbHeight;
    pbCross_ = row_ ? box_.pbHeight : box_.pbWidth;
    pbMainStart_ = row_ ? p.left + b.left : p.top + b.top;
    pbCrossStart_ = row_ ? p.top + b.top : p.left + b.left;
    gapMain_ = nonNegative(row_ ? style_.columnGap : style_.rowGap);
    gapCross_ = nonNegative(row_ ? style_.rowGap : style_.columnGap);
    minMain_ = row_ ? box_.minWidth : box_.minHeight;
    maxMain_ = row_ ? box_.maxWidth : box_.maxHeight;
    minCross_ = row_ ? box_.minHeight : box_.minWidth;
    maxCross_ = row_ ? box_.maxHeight : box_.maxWidth;
    knownMain_ = row_ ? box_.knownWidth : box_.knownHeight;
    knownCross_ = row_ ? box_.knownHeight : box_.knownWidth;
    // 9.2.2: the available main and cross space for the items.
    const AvailableSpace width = innerAvailable(box_.knownWidth, in.availableWidth, box_.margin.horizontal(),
                                                box_.minWidth, box_.maxWidth, box_.pbWidth);
    const AvailableSpace height = innerAvailable(box_.knownHeight, in.availableHeight, box_.margin.vertical(),
                                                 box_.minHeight, box_.maxHeight, box_.pbHeight);
    availMain_ = row_ ? width : height;
    availCross_ = row_ ? height : width;
    if (knownMain_)
        innerMain_ = std::max(0.0f, *knownMain_ - pbMain_);
    if (knownCross_)
        innerCross_ = std::max(0.0f, *knownCross_ - pbCross_);
    contentSize_ = {p.horizontal(), p.vertical()};
}

Size FlexAlgorithm::run()
{
    if (in_.mode == RunMode::ComputeSize && knownMain_ && knownCross_)
        return sizeOf(*knownMain_, *knownCross_);

    collectChildren();
    for (FlexItem &item : s_.items)
        computeBaseSize(item);

    // 9.2.4: the main size of the container. A row (the inline axis) is fit-content: its
    // max-content size, at most the available space, at least its min-content size (9.9.1). It is
    // known before the items are broken into lines.
    const bool mainBeforeLines = knownMain_ || row_;
    if (mainBeforeLines) {
        main_ = knownMain_ ? *knownMain_ : clampContainerMain(rowContentMain());
        innerMain_ = std::max(0.0f, main_ - pbMain_);
        if (in_.mode == RunMode::ComputeSize && knownCross_)
            return sizeOf(main_, *knownCross_);
        if (!knownMain_) {
            // The percentages that acted as auto for the row's intrinsic width resolve against
            // the width it got (as browsers lay out a shrink-to-fit box).
            contentBlock_ = innerSize();
            for (FlexItem &item : s_.items) {
                if (!item.mainPercentages)
                    continue;
                const NodeId child = item.node;
                item = FlexItem{};
                initItem(item, child);
                computeBaseSize(item);
            }
        }
    }

    // 9.3.5: lines. A column without a definite height breaks at its max-height, else not at all
    // (as browsers: an auto block size is the content's).
    const float limit = innerMain_ ? *innerMain_ : maxMain_ ? std::max(0.0f, *maxMain_ - pbMain_) : kInfinity;
    collectLines(limit);

    if (!mainBeforeLines) {
        // A column's height is its content's: the longest line of hypothetical main sizes.
        float longest = 0;
        for (const FlexLine &line : s_.lines)
            longest = std::max(longest, lineOuterHypotheticalMain(line));
        main_ = clampContainerMain(longest);
        innerMain_ = std::max(0.0f, main_ - pbMain_);
        if (in_.mode == RunMode::ComputeSize && knownCross_)
            return sizeOf(main_, *knownCross_);
    }

    // 9.3.6: resolving the flexible lengths.
    for (const FlexLine &line : s_.lines)
        resolveFlexibleLengths(line);

    // 9.4.7–8: hypothetical cross sizes and the lines' cross sizes. A single line in a container
    // of definite cross size is that size, so the items it stretches need no measuring.
    const bool singleDefinite = !wrap_ && innerCross_.has_value();
    for (FlexLine &line : s_.lines) {
        float lineCross = 0;
        for (FlexItem &item : lineItems(line)) {
            if (singleDefinite && stretched(item))
                continue;
            computeHypotheticalCross(item);
            lineCross = std::max(lineCross, item.hypotheticalCross + item.marginCrossStart + item.marginCrossEnd);
        }
        if (singleDefinite)
            lineCross = *innerCross_;
        else if (!wrap_) // a single line is clamped by the container's min and max cross size
            lineCross = std::max(0.0f, clampOpt(lineCross, minus(minCross_, pbCross_), minus(maxCross_, pbCross_)));
        line.crossSize = lineCross;
    }

    // 9.4.9: align-content: stretch grows the lines to a definite (or minimum) cross size.
    const auto lineCount = float(s_.lines.size());
    if (wrap_ && style_.alignContent == AlignContent::Stretch) {
        if (const OptFloat target = knownCross_ ? knownCross_ : minCross_) {
            const float inner = std::max(0.0f, clampOpt(*target, minCross_, maxCross_) - pbCross_);
            float total = gapCross_ * (lineCount - 1);
            for (const FlexLine &line : s_.lines)
                total += line.crossSize;
            if (total < inner) {
                const float extra = (inner - total) / lineCount;
                for (FlexLine &line : s_.lines)
                    line.crossSize += extra;
            }
        }
    }

    // 9.4.15: the container's cross size.
    float linesCross = gapCross_ * (lineCount - 1);
    for (const FlexLine &line : s_.lines)
        linesCross += line.crossSize;
    cross_ = knownCross_ ? *knownCross_ : std::max(pbCross_, clampOpt(linesCross + pbCross_, minCross_, maxCross_));
    innerCross_ = std::max(0.0f, cross_ - pbCross_);
    if (in_.mode == RunMode::ComputeSize)
        return sizeOf(main_, cross_);

    // 9.6.16: align-content places the lines (a multi-line container only).
    {
        const float freeSpace = *innerCross_ - linesCross;
        float start = 0;
        float between = 0;
        if (wrap_) {
            switch (style_.alignContent) {
            case AlignContent::Stretch:
            case AlignContent::FlexStart: break;
            case AlignContent::FlexEnd: start = freeSpace; break;
            case AlignContent::Center: start = freeSpace / 2; break;
            case AlignContent::SpaceBetween:
                if (freeSpace > 0 && lineCount > 1)
                    between = freeSpace / (lineCount - 1);
                break;
            case AlignContent::SpaceAround: // fallback safe center: flex-start when overflowing
                if (freeSpace > 0) {
                    between = freeSpace / lineCount;
                    start = between / 2;
                }
                break;
            }
        }
        float cursor = start;
        for (FlexLine &line : s_.lines) {
            line.crossOffset = cursor;
            cursor += line.crossSize + gapCross_ + between;
        }
    }

    // 9.4.11 (the used cross sizes, stretching), 9.5.12 (main-axis alignment), 9.6.13–14 (cross).
    for (const FlexLine &line : s_.lines) {
        alignMain(line);
        for (FlexItem &item : lineItems(line)) {
            item.targetCross = stretched(item)
                                       ? std::max(item.pbCross,
                                                  clampOpt(line.crossSize - item.marginCrossStart - item.marginCrossEnd,
                                                           item.minCross, item.maxCross))
                                       : item.hypotheticalCross;
            alignCross(line, item);
        }
    }

    // The items' own layouts at their final sizes (a stretched item is laid out again with its
    // definite cross size: text in a column wraps at the column's width), then absolute children.
    for (const FlexItem &item : s_.items)
        layoutItem(item);
    for (const NodeId child : s_.absolute)
        layoutAbsolute(child);

    Node &node = e_.nodes[id_];
    node.layout.rect.size = sizeOf(main_, cross_);
    node.layout.contentSize = contentSize_;
    return sizeOf(main_, cross_);
}

void FlexAlgorithm::collectChildren()
{
    s_.items.clear();
    s_.lines.clear();
    s_.absolute.clear();
    contentBlock_ = innerSize();
    for (const NodeId child : e_.childrenOf(e_.nodes[id_])) {
        const LayoutStyle &cs = e_.nodes[child].style;
        if (cs.display == Display::None) {
            if (in_.mode == RunMode::PerformLayout)
                e_.hide(child);
            continue;
        }
        if (cs.position == PositionType::Absolute) {
            s_.absolute.push_back(child);
            continue;
        }
        initItem(s_.items.emplace_back(), child);
    }
}

void FlexAlgorithm::initItem(FlexItem &item, NodeId child) const
{
    const LayoutStyle &cs = e_.nodes[child].style;
    item.node = child;
    item.align = resolvedAlign(cs.alignSelf, style_.alignItems);
    item.grow = flexFactor(cs.flexGrow, 0);
    item.shrink = flexFactor(cs.flexShrink, 1);
    // 4.5: a scroll container's automatic minimum size is 0 (hidden in one axis makes the other
    // auto, so either axis counts).
    item.scrollContainer = cs.overflowX != Overflow::Visible || cs.overflowY != Overflow::Visible;
    const Edges padding = nonNegative(cs.padding);
    const Edges border = nonNegative(cs.border);
    const float pbWidth = padding.horizontal() + border.horizontal();
    const float pbHeight = padding.vertical() + border.vertical();
    item.pbMain = row_ ? pbWidth : pbHeight;
    item.pbCross = row_ ? pbHeight : pbWidth;

    // Percentages are of the container's content box (definite or not yet), margins' of its width.
    const OptSize cb = contentBlock_;
    const OptFloat minWidth = resolveMinSize(cs.minWidth, cb.width);
    const OptFloat maxWidth = resolveSize(cs.maxWidth, cb.width);
    const OptFloat minHeight = resolveMinSize(cs.minHeight, cb.height);
    const OptFloat maxHeight = resolveSize(cs.maxHeight, cb.height);
    OptFloat width = resolveSize(cs.width, cb.width);
    OptFloat height = resolveSize(cs.height, cb.height);
    item.aspect = aspectRatio(cs);
    if (item.aspect) { // from the used size of the other axis (CSS Sizing 4: after its min/max)
        if (width && !height)
            height = clampLength(clampOpt(*width, minWidth, maxWidth) / *item.aspect);
        else if (height && !width)
            width = clampLength(clampOpt(*height, minHeight, maxHeight) * *item.aspect);
    }
    item.sizeMain = row_ ? width : height;
    item.sizeCross = row_ ? height : width;
    item.minMain = row_ ? minWidth : minHeight;
    item.minMainAuto = !item.minMain;
    item.maxMain = row_ ? maxWidth : maxHeight;
    item.minCross = row_ ? minHeight : minWidth;
    item.maxCross = row_ ? maxHeight : maxWidth;
    // Percentages along a row's main axis (all margins are of the width) wait for its width.
    const auto percent = [](Length l) { return l.unit == Length::Unit::Percent; };
    item.mainPercentages = row_
                           && (percent(cs.flexBasis) || percent(cs.width) || percent(cs.minWidth)
                               || percent(cs.maxWidth) || percent(cs.margin.top) || percent(cs.margin.right)
                               || percent(cs.margin.bottom) || percent(cs.margin.left));

    const OptFloat top = resolveEdge(cs.margin.top, cb.width);
    const OptFloat right = resolveEdge(cs.margin.right, cb.width);
    const OptFloat bottom = resolveEdge(cs.margin.bottom, cb.width);
    const OptFloat left = resolveEdge(cs.margin.left, cb.width);
    const OptFloat mainStart = row_ ? left : top;
    const OptFloat mainEnd = row_ ? right : bottom;
    const OptFloat crossStart = row_ ? top : left;
    const OptFloat crossEnd = row_ ? bottom : right;
    item.autoMainStart = !mainStart;
    item.autoMainEnd = !mainEnd;
    item.autoCrossStart = !crossStart;
    item.autoCrossEnd = !crossEnd;
    item.marginMainStart = mainStart.value_or(0);
    item.marginMainEnd = mainEnd.value_or(0);
    item.marginCrossStart = crossStart.value_or(0);
    item.marginCrossEnd = crossEnd.value_or(0);

    // 9.2.3 E, 9.8: the content is measured with the item's own cross size, or with the line's
    // when a single line of definite cross size stretches it — where the main size depends on it:
    // in a column (a height follows the width it wraps at) or through an aspect ratio. A row's
    // items are measured without, so their measurements stay the same when the row's height
    // becomes known.
    if (item.sizeCross)
        item.contentKnownCross = std::max(item.pbCross, clampOpt(*item.sizeCross, item.minCross, item.maxCross));
    else if (!wrap_ && innerCross_ && stretched(item) && (!row_ || item.aspect))
        item.contentKnownCross =
                std::max(item.pbCross, clampOpt(*innerCross_ - item.marginCrossStart - item.marginCrossEnd,
                                                item.minCross, item.maxCross));
}

float FlexAlgorithm::measureContentMain(const FlexItem &item, AvailableSpace::Kind kind)
{
    LayoutInput q;
    q.known = optSizeOf(std::nullopt, item.contentKnownCross);
    q.parent = contentBlock_;
    const AvailableSpace mainSpace =
            kind == AvailableSpace::Kind::MinContent ? AvailableSpace::minContent() : AvailableSpace::maxContent();
    // Intrinsic widths do not depend on the height available (CSS Sizing 3, 5.1), so a row's items
    // are measured the same way while the row is measured and once its height is known; intrinsic
    // heights depend on the width a column offers.
    q.availableWidth = row_ ? mainSpace : availCross_;
    q.availableHeight = row_ ? AvailableSpace::maxContent() : mainSpace;
    q.mode = RunMode::ComputeSize;
    q.sizing = SizingMode::ContentSize;
    return mainOf(e_.computeNode(item.node, q));
}

// 9.2.3: the flex base size and the hypothetical main size.
void FlexAlgorithm::computeBaseSize(FlexItem &item)
{
    const Length basisLength = e_.nodes[item.node].style.flexBasis;
    OptFloat basis;
    switch (basisLength.unit) {
    case Length::Unit::Px:
        basis = resolveSize(basisLength, std::nullopt);
        if (!basis) // invalid: auto
            basis = item.sizeMain;
        break;
    case Length::Unit::Percent: // of an indefinite size: content (7.2.3)
        basis = resolveSize(basisLength, innerMain_);
        break;
    case Length::Unit::Auto: // the main size property; auto: content
        basis = item.sizeMain;
        break;
    }
    // B: an aspect ratio and a definite cross size.
    if (!basis && item.aspect && item.contentKnownCross)
        basis = clampLength(row_ ? *item.contentKnownCross * *item.aspect : *item.contentKnownCross / *item.aspect);
    if (basis) {
        item.basis = std::max(*basis, item.pbMain);
        item.basisFromContent = false;
    } else {
        // E: the content's max-content size (min-content when the container itself is being
        // sized under a min-content constraint).
        item.basisKind = availMain_.kind == AvailableSpace::Kind::MinContent ? AvailableSpace::Kind::MinContent
                                                                             : AvailableSpace::Kind::MaxContent;
        item.basis = std::max(measureContentMain(item, item.basisKind), item.pbMain);
        item.basisFromContent = true;
    }
    item.innerBasis = item.basis - item.pbMain;
    // The base size clamped by the used min and max main sizes. A content-based base size is never
    // below the automatic minimum (min-content ≤ max-content), so that one is not measured here.
    if (item.basisFromContent && item.minMainAuto)
        item.hypotheticalMain = item.maxMain ? std::min(item.basis, *item.maxMain) : item.basis;
    else
        item.hypotheticalMain = clampOpt(item.basis, minMainSize(item), item.maxMain);
    item.hypotheticalMain = std::max(item.hypotheticalMain, item.pbMain);
}

// 4.5: the used min main size; auto is the automatic minimum size — the min-content size, capped
// by the specified size and the max main size, 0 for scroll containers. Measured on first use.
float FlexAlgorithm::minMainSize(FlexItem &item)
{
    if (item.minResolved)
        return item.resolvedMin;
    float minimum = 0;
    if (!item.minMainAuto) {
        minimum = *item.minMain;
    } else if (!item.scrollContainer) {
        float content = item.basisFromContent && item.basisKind == AvailableSpace::Kind::MinContent
                                ? item.basis
                                : measureContentMain(item, AvailableSpace::Kind::MinContent);
        if (item.sizeMain) // the specified size suggestion
            content = std::min(content, *item.sizeMain);
        else if (item.aspect && item.contentKnownCross) // the transferred size suggestion
            content = std::min(content,
                               row_ ? *item.contentKnownCross * *item.aspect : *item.contentKnownCross / *item.aspect);
        if (item.maxMain)
            content = std::min(content, *item.maxMain);
        minimum = content;
    }
    item.resolvedMin = std::max(minimum, item.pbMain);
    item.minResolved = true;
    return item.resolvedMin;
}

// 9.9.1: an item's outer min-content or max-content contribution to its container's main size.
float FlexAlgorithm::contribution(FlexItem &item, bool minContent)
{
    const AvailableSpace::Kind kind = minContent ? AvailableSpace::Kind::MinContent : AvailableSpace::Kind::MaxContent;
    float content;
    if (item.sizeMain)
        content = *item.sizeMain;
    else if (item.basisFromContent && item.basisKind == kind)
        content = item.basis;
    else
        content = std::max(measureContentMain(item, kind), item.pbMain);
    // As browsers do (the 2016 change of 9.9.1): the flex base size is a floor for an item that
    // cannot shrink and a ceiling for one that cannot grow.
    const float clampingBasis = item.sizeMain ? std::max(item.basis, *item.sizeMain) : item.basis;
    float ceiling = item.maxMain.value_or(kInfinity);
    if (item.grow == 0)
        ceiling = std::min(ceiling, clampingBasis);
    float v = std::min(content, ceiling);
    if (item.shrink == 0)
        v = std::max(v, clampingBasis);
    // The automatic minimum is ≤ the content: it matters only when the content was capped.
    if (!item.minMainAuto || v < content)
        v = std::max(v, minMainSize(item));
    return std::max(v, item.pbMain) + item.marginMainStart + item.marginMainEnd;
}

float FlexAlgorithm::rowContentMain()
{
    const std::size_t count = s_.items.size();
    const float gaps = count > 1 ? gapMain_ * float(count - 1) : 0.0f;
    const auto sum = [this, gaps](bool minContent) {
        float total = gaps;
        for (FlexItem &item : s_.items)
            total += contribution(item, minContent);
        return total;
    };
    const auto largestMin = [this]() {
        float largest = 0;
        for (FlexItem &item : s_.items)
            largest = std::max(largest, contribution(item, true));
        return largest;
    };
    switch (availMain_.kind) {
    case AvailableSpace::Kind::MaxContent: return sum(false);
    case AvailableSpace::Kind::MinContent: // a wrapping row puts each item on a line of its own
        return wrap_ ? largestMin() : sum(true);
    case AvailableSpace::Kind::Definite: {
        const float maxContent = sum(false);
        if (maxContent <= availMain_.value)
            return maxContent;
        return std::max(availMain_.value, wrap_ ? largestMin() : sum(true));
    }
    }
    return 0;
}

float FlexAlgorithm::clampContainerMain(float innerContent) const
{
    return std::max(pbMain_, clampOpt(innerContent + pbMain_, minMain_, maxMain_));
}

void FlexAlgorithm::collectLines(float limit)
{
    s_.lines.clear();
    const auto count = std::uint32_t(s_.items.size());
    if (!wrap_) {
        s_.lines.push_back({0, count});
        return;
    }
    std::uint32_t begin = 0;
    float used = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const FlexItem &item = s_.items[i];
        const float outer = item.hypotheticalMain + item.marginMainStart + item.marginMainEnd;
        if (i > begin && used + gapMain_ + outer > limit + kLineEpsilon) {
            s_.lines.push_back({begin, i});
            begin = i;
            used = outer;
        } else {
            used += (i > begin ? gapMain_ : 0.0f) + outer;
        }
    }
    s_.lines.push_back({begin, count});
}

float FlexAlgorithm::lineOuterHypotheticalMain(const FlexLine &line) const
{
    const std::uint32_t count = line.end - line.begin;
    float total = count > 1 ? gapMain_ * float(count - 1) : 0.0f;
    for (std::uint32_t i = line.begin; i < line.end; ++i) {
        const FlexItem &item = s_.items[i];
        total += item.hypotheticalMain + item.marginMainStart + item.marginMainEnd;
    }
    return total;
}

// 9.7: resolving flexible lengths — the freeze loop. Sums run in double: flex factors may be
// huge, and a shrink factor is scaled by the inner base size.
void FlexAlgorithm::resolveFlexibleLengths(const FlexLine &line)
{
    const std::span<FlexItem> items = lineItems(line);
    if (items.empty())
        return;
    const double inner = *innerMain_;
    const double gaps = double(gapMain_) * double(items.size() - 1);

    // 1. Grow or shrink, by the sum of the outer hypothetical main sizes.
    double hypotheticalSum = gaps;
    for (const FlexItem &item : items)
        hypotheticalSum += double(item.hypotheticalMain) + item.marginMainStart + item.marginMainEnd;
    // A line that fits exactly ends at the hypothetical sizes (shrinking by nothing leaves each
    // item at its base size clamped by its min and max), without measuring any minimum.
    if (std::abs(inner - hypotheticalSum) <= 1e-6 * std::max(1.0, inner)) {
        for (FlexItem &item : items)
            item.targetMain = item.hypotheticalMain;
        return;
    }
    const bool growing = hypotheticalSum < inner;

    // 2. Size inflexible items.
    for (FlexItem &item : items) {
        item.targetMain = item.hypotheticalMain;
        const float factor = growing ? item.grow : item.shrink;
        item.frozen = factor == 0 || (growing && item.basis > item.hypotheticalMain)
                      || (!growing && item.basis < item.hypotheticalMain);
    }

    // 3. The initial free space: frozen items at their target size, the others at their base size.
    const auto freeSpace = [&items, inner, gaps]() {
        double used = gaps;
        for (const FlexItem &item : items)
            used += double(item.frozen ? item.targetMain : item.basis) + item.marginMainStart + item.marginMainEnd;
        return inner - used;
    };
    const double initialFree = freeSpace();

    // 4. Loop; every round freezes at least one item.
    for (std::size_t round = 0; round <= items.size(); ++round) {
        double factorSum = 0;
        double scaledShrinkSum = 0;
        bool unfrozen = false;
        for (const FlexItem &item : items) {
            if (item.frozen)
                continue;
            unfrozen = true;
            factorSum += growing ? item.grow : item.shrink;
            scaledShrinkSum += double(item.shrink) * item.innerBasis;
        }
        if (!unfrozen)
            break;

        // b. The remaining free space, less when the factors sum below 1.
        double remaining = freeSpace();
        if (factorSum < 1) {
            const double scaled = initialFree * factorSum;
            if (std::abs(scaled) < std::abs(remaining))
                remaining = scaled;
        }

        // c. Distribute it by the factors (shrinking: by the factor × the inner base size);
        // d. clamp by min/max, floored at padding + border, and record the violations.
        double totalViolation = 0;
        for (FlexItem &item : items) {
            if (item.frozen)
                continue;
            double target = item.basis;
            if (remaining != 0) {
                if (growing) {
                    if (factorSum > 0)
                        target += remaining * (double(item.grow) / factorSum);
                } else if (scaledShrinkSum > 0) {
                    target += remaining * (double(item.shrink) * item.innerBasis / scaledShrinkSum);
                }
            }
            const auto unclamped = float(std::clamp(target, -double(kMaxLength), double(kMaxLength)));
            // A growing content-sized item stays above its automatic minimum (which is then not measured).
            const float minimum =
                    growing && item.basisFromContent && item.minMainAuto ? item.pbMain : minMainSize(item);
            const float clamped = std::max(item.pbMain, clampOpt(unclamped, minimum, item.maxMain));
            item.violation = clamped - unclamped;
            item.targetMain = clamped;
            totalViolation += item.violation;
        }

        // e. Freeze all (no violation), the min violators (positive) or the max violators (negative).
        for (FlexItem &item : items) {
            if (item.frozen)
                continue;
            if (totalViolation == 0 || (totalViolation > 0 && item.violation > 0)
                || (totalViolation < 0 && item.violation < 0))
                item.frozen = true;
        }
    }
}

// 9.4.7: the cross size from laying the item out at its main size, auto as fit-content.
void FlexAlgorithm::computeHypotheticalCross(FlexItem &item)
{
    if (item.sizeCross) {
        item.hypotheticalCross = std::max(item.pbCross, clampOpt(*item.sizeCross, item.minCross, item.maxCross));
        return;
    }
    LayoutInput q;
    q.known = optSizeOf(item.targetMain, std::nullopt);
    q.parent = innerSize();
    q.availableWidth = row_ ? availMain_ : availCross_;
    q.availableHeight = row_ ? availCross_ : availMain_;
    q.mode = RunMode::ComputeSize;
    q.sizing = SizingMode::InherentSize;
    const Size size = e_.computeNode(item.node, q);
    item.hypotheticalCross = std::max(item.pbCross, clampOpt(crossOf(size), item.minCross, item.maxCross));
}

// 9.5.12: auto margins take positive free space; justify-content distributes what is left. The
// reverse directions run the same flow from the main-end edge.
void FlexAlgorithm::alignMain(const FlexLine &line)
{
    const std::span<FlexItem> items = lineItems(line);
    if (items.empty())
        return;
    const auto count = float(items.size());
    float used = gapMain_ * (count - 1);
    int autoMargins = 0;
    for (const FlexItem &item : items) {
        used += item.targetMain + item.marginMainStart + item.marginMainEnd;
        autoMargins += int(item.autoMainStart) + int(item.autoMainEnd);
    }
    float freeSpace = *innerMain_ - used;
    float autoMargin = 0;
    if (autoMargins > 0 && freeSpace > 0) {
        autoMargin = freeSpace / float(autoMargins);
        freeSpace = 0;
    }
    float start = 0;
    float between = 0;
    switch (style_.justifyContent) {
    case JustifyContent::FlexStart: break;
    case JustifyContent::FlexEnd: start = freeSpace; break;
    case JustifyContent::Center: start = freeSpace / 2; break;
    // The space-* values fall back as CSS Box Alignment 3 (and browsers) say: space-between to
    // flex-start, the others to safe center — flex-start too when the items overflow.
    case JustifyContent::SpaceBetween:
        if (freeSpace > 0 && count > 1)
            between = freeSpace / (count - 1);
        break;
    case JustifyContent::SpaceAround:
        if (freeSpace > 0) {
            between = freeSpace / count;
            start = between / 2;
        }
        break;
    case JustifyContent::SpaceEvenly:
        if (freeSpace > 0) {
            between = freeSpace / (count + 1);
            start = between;
        }
        break;
    }
    float cursor = start;
    for (FlexItem &item : items) {
        item.usedMarginMainStart = item.autoMainStart ? autoMargin : item.marginMainStart;
        item.usedMarginMainEnd = item.autoMainEnd ? autoMargin : item.marginMainEnd;
        cursor += reverse_ ? item.usedMarginMainEnd : item.usedMarginMainStart;
        item.offsetMain = reverse_ ? *innerMain_ - cursor - item.targetMain : cursor;
        cursor += item.targetMain + (reverse_ ? item.usedMarginMainStart : item.usedMarginMainEnd) + gapMain_ + between;
    }
}

// 9.6.13–14: auto margins in the cross axis, then align-self within the line.
void FlexAlgorithm::alignCross(const FlexLine &line, FlexItem &item) const
{
    const float freeSpace = line.crossSize - item.targetCross - item.marginCrossStart - item.marginCrossEnd;
    float offset = item.marginCrossStart;
    item.usedMarginCrossStart = item.marginCrossStart;
    item.usedMarginCrossEnd = item.marginCrossEnd;
    if (item.autoCrossStart || item.autoCrossEnd) {
        // Positive free space is shared by the auto margins; otherwise an auto start margin is 0.
        const int autos = int(item.autoCrossStart) + int(item.autoCrossEnd);
        const float each = freeSpace > 0 ? freeSpace / float(autos) : 0.0f;
        if (item.autoCrossStart)
            item.usedMarginCrossStart = each;
        if (item.autoCrossEnd)
            item.usedMarginCrossEnd = each;
        offset = item.usedMarginCrossStart;
    } else {
        switch (item.align) {
        case AlignItems::FlexEnd: offset = line.crossSize - item.marginCrossEnd - item.targetCross; break;
        case AlignItems::Center: offset = item.marginCrossStart + freeSpace / 2; break;
        case AlignItems::Auto:
        case AlignItems::Stretch:
        case AlignItems::FlexStart:
        case AlignItems::Baseline: break;
        }
    }
    item.offsetCross = line.crossOffset + offset;
}

void FlexAlgorithm::layoutItem(const FlexItem &item)
{
    const OptSize inner = innerSize();
    LayoutInput q;
    q.known = optSizeOf(item.targetMain, item.targetCross);
    q.parent = inner;
    q.availableWidth = AvailableSpace::definite(inner.width.value_or(0));
    q.availableHeight = AvailableSpace::definite(inner.height.value_or(0));
    q.mode = RunMode::PerformLayout;
    q.sizing = SizingMode::InherentSize;
    e_.computeNode(item.node, q);

    Node &child = e_.nodes[item.node];
    const float mainPosition = clampLength(pbMainStart_ + item.offsetMain);
    const float crossPosition = clampLength(pbCrossStart_ + item.offsetCross);
    child.layout.rect.origin = row_ ? Point{mainPosition, crossPosition} : Point{crossPosition, mainPosition};
    const Edges margin = row_ ? Edges{item.usedMarginCrossStart, item.usedMarginMainEnd, item.usedMarginCrossEnd,
                                      item.usedMarginMainStart}
                              : Edges{item.usedMarginMainStart, item.usedMarginCrossEnd, item.usedMarginMainEnd,
                                      item.usedMarginCrossStart};
    extendContent(child.layout.rect, margin);
}

// An absolutely positioned child (4.1): out of flow, placed by its insets against the
// container's padding box; an axis with both insets auto takes its static position — where it
// would be as the container's sole item (justify-content in the main axis, align-self in the
// cross axis).
void FlexAlgorithm::layoutAbsolute(NodeId child)
{
    const LayoutStyle &cs = e_.nodes[child].style;
    const Edges &border = box_.border;
    const Edges &padding = box_.padding;
    const Size container = sizeOf(main_, cross_);
    const float cbWidth = std::max(0.0f, container.width - border.horizontal());
    const float cbHeight = std::max(0.0f, container.height - border.vertical());

    const OptFloat left = resolveEdge(cs.inset.left, cbWidth);
    const OptFloat right = resolveEdge(cs.inset.right, cbWidth);
    const OptFloat top = resolveEdge(cs.inset.top, cbHeight);
    const OptFloat bottom = resolveEdge(cs.inset.bottom, cbHeight);
    const OptFloat autoLeft = resolveEdge(cs.margin.left, cbWidth);
    const OptFloat autoRight = resolveEdge(cs.margin.right, cbWidth);
    const OptFloat autoTop = resolveEdge(cs.margin.top, cbWidth);
    const OptFloat autoBottom = resolveEdge(cs.margin.bottom, cbWidth);
    Edges margin{autoTop.value_or(0), autoRight.value_or(0), autoBottom.value_or(0), autoLeft.value_or(0)};

    const Edges childPadding = nonNegative(cs.padding);
    const Edges childBorder = nonNegative(cs.border);
    const float pbWidth = childPadding.horizontal() + childBorder.horizontal();
    const float pbHeight = childPadding.vertical() + childBorder.vertical();
    const OptFloat minWidth = resolveMinSize(cs.minWidth, cbWidth);
    const OptFloat maxWidth = resolveSize(cs.maxWidth, cbWidth);
    const OptFloat minHeight = resolveMinSize(cs.minHeight, cbHeight);
    const OptFloat maxHeight = resolveSize(cs.maxHeight, cbHeight);
    OptFloat width = resolveSize(cs.width, cbWidth);
    OptFloat height = resolveSize(cs.height, cbHeight);
    const OptFloat ratio = aspectRatio(cs);
    // The size: its own, else between both insets (CSS 2 10.3.7, 10.6.4), else its content; an
    // aspect ratio transfers the used size of the other axis.
    if (width)
        width = std::max(pbWidth, clampOpt(*width, minWidth, maxWidth));
    else if (left && right)
        width = std::max(pbWidth, clampOpt(cbWidth - *left - *right - margin.horizontal(), minWidth, maxWidth));
    if (height)
        height = std::max(pbHeight, clampOpt(*height, minHeight, maxHeight));
    else if (top && bottom)
        height = std::max(pbHeight, clampOpt(cbHeight - *top - *bottom - margin.vertical(), minHeight, maxHeight));
    if (ratio) {
        if (width && !height)
            height = std::max(pbHeight, clampOpt(clampLength(*width / *ratio), minHeight, maxHeight));
        else if (height && !width)
            width = std::max(pbWidth, clampOpt(clampLength(*height * *ratio), minWidth, maxWidth));
    }
    Size size;
    if (width && height) {
        size = {*width, *height};
    } else {
        LayoutInput q;
        q.known = {width, height};
        q.parent = {cbWidth, cbHeight};
        q.availableWidth = AvailableSpace::definite(std::max(0.0f, cbWidth - left.value_or(0) - right.value_or(0)));
        q.availableHeight = AvailableSpace::definite(std::max(0.0f, cbHeight - top.value_or(0) - bottom.value_or(0)));
        q.mode = RunMode::ComputeSize;
        q.sizing = SizingMode::InherentSize;
        size = e_.computeNode(child, q);
    }
    LayoutInput f;
    f.known = {size.width, size.height};
    f.parent = {cbWidth, cbHeight};
    f.availableWidth = AvailableSpace::definite(cbWidth);
    f.availableHeight = AvailableSpace::definite(cbHeight);
    f.mode = RunMode::PerformLayout;
    f.sizing = SizingMode::InherentSize;
    e_.computeNode(child, f);

    // Auto margins with both insets set take the rest: shared, or (horizontally, when it is
    // negative) all at the right.
    if (left && right && (!autoLeft || !autoRight)) {
        const float rest = cbWidth - *left - *right - size.width - margin.horizontal();
        if (!autoLeft && !autoRight) {
            margin.left = rest >= 0 ? rest / 2 : 0.0f;
            margin.right = rest >= 0 ? rest / 2 : rest;
        } else if (!autoLeft) {
            margin.left = rest;
        } else {
            margin.right = rest;
        }
    }
    if (top && bottom && (!autoTop || !autoBottom)) {
        const float rest = cbHeight - *top - *bottom - size.height - margin.vertical();
        if (!autoTop && !autoBottom) {
            margin.top = rest / 2;
            margin.bottom = rest / 2;
        } else if (!autoTop) {
            margin.top = rest;
        } else {
            margin.bottom = rest;
        }
    }

    Place mainPlace = Place::Start;
    switch (style_.justifyContent) {
    case JustifyContent::FlexStart:
    case JustifyContent::SpaceBetween: break;
    case JustifyContent::FlexEnd: mainPlace = Place::End; break;
    case JustifyContent::Center:
    case JustifyContent::SpaceAround:
    case JustifyContent::SpaceEvenly: mainPlace = Place::Center; break;
    }
    if (reverse_ && mainPlace != Place::Center)
        mainPlace = mainPlace == Place::Start ? Place::End : Place::Start;
    Place crossPlace = Place::Start;
    switch (resolvedAlign(cs.alignSelf, style_.alignItems)) {
    case AlignItems::FlexEnd: crossPlace = Place::End; break;
    case AlignItems::Center: crossPlace = Place::Center; break;
    case AlignItems::Auto:
    case AlignItems::Stretch:
    case AlignItems::FlexStart:
    case AlignItems::Baseline: break;
    }
    const Place horizontal = row_ ? mainPlace : crossPlace;
    const Place vertical = row_ ? crossPlace : mainPlace;

    float x;
    if (left)
        x = border.left + *left + margin.left;
    else if (right)
        x = container.width - border.right - *right - margin.right - size.width;
    else
        x = placed(horizontal, border.left + padding.left, container.width - border.right - padding.right, size.width,
                   margin.left, margin.right);
    float y;
    if (top)
        y = border.top + *top + margin.top;
    else if (bottom)
        y = container.height - border.bottom - *bottom - margin.bottom - size.height;
    else
        y = placed(vertical, border.top + padding.top, container.height - border.bottom - padding.bottom, size.height,
                   margin.top, margin.bottom);

    Node &node = e_.nodes[child];
    node.layout.rect.origin = {clampLength(x), clampLength(y)};
    extendContent(node.layout.rect, margin);
}

// The content size (for scroll containers): the children's margin boxes and the end padding,
// measured from the padding box's origin.
void FlexAlgorithm::extendContent(const Rect &rect, const Edges &margin)
{
    const Edges &border = box_.border;
    const Edges &padding = box_.padding;
    contentSize_.width =
            std::max(contentSize_.width, clampLength(rect.right() + margin.right - border.left + padding.right));
    contentSize_.height =
            std::max(contentSize_.height, clampLength(rect.bottom() + margin.bottom - border.top + padding.bottom));
}

} // namespace

Size Engine::computeFlex(NodeId id, const LayoutInput &in)
{
    if (scratch.size() <= depth)
        scratch.emplace_back();
    Scratch &level = scratch[depth];
    ++depth;
    const Size size = FlexAlgorithm(*this, id, in, level).run();
    --depth;
    return size;
}

} // namespace brulion::atrament::layout_detail
