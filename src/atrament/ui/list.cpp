#include "ui/list.h"

#include <algorithm>
#include <cmath>

namespace brulion::atrament {

// Which row had the focus in the last frame (kept across frames as the list's element state).
struct UniformList::FocusState {
    std::optional<std::size_t> row;
};

UniformList::UniformList(ElementId id, std::size_t count, RenderItem render)
    : m_id(id)
    , m_count(count)
    , m_render(render)
{
    m_style.layout.overflowY = Overflow::Scroll;
}

UniformList &&UniformList::ariaLabel(std::u16string_view name) &&
{
    m_label.assign(name.begin(), name.end());
    return std::move(*this);
}

NodeId UniformList::requestLayout(Window &window, App &)
{
    // Its rows are laid out on their own in prepaint: the list is a leaf sized by its style.
    m_layout = window.requestLayout(m_style.layout, {});
    return m_layout;
}

void UniformList::prepaint(Rect bounds, Window &window, App &app)
{
    m_hitbox = window.insertHitbox(bounds);
    m_focusState = &window.elementState<FocusState>();
    ScrollState &st = m_scrollHandle ? *m_scrollHandle : window.elementState<ScrollState>();
    m_scroll = &st;
    const Rect inner = bounds.inset(m_style.layout.padding).inset(m_style.layout.border);
    if (!m_render || m_count == 0) {
        st.viewport = inner.size;
        st.content = {};
        st.scrollBy({});
        window.pushScrollFrame(st, inner, false, true, m_hitbox);
        window.popScrollFrame();
        return;
    }
    float rowHeight = m_itemHeight.value_or(0);
    if (rowHeight <= 0) {
        // The first row measured at the list's width (and dropped: it is laid out again where it
        // is shown, if it is).
        AnyElement probe = m_render(0, window, app);
        probe.setSiblingIndex(0);
        rowHeight = probe.layoutAsRoot(AvailableSpace::definite(inner.width()), AvailableSpace::maxContent(), window, app)
                            .height;
        rowHeight = std::max(rowHeight, 1.0f);
    }
    st.viewport = inner.size;
    st.content = { inner.width(), rowHeight * float(m_count) };
    st.scrollBy({});

    const float top = st.offset.y;
    m_first = std::min(m_count - 1, std::size_t(std::max(0.0f, std::floor(top / rowHeight))));
    const std::size_t last = std::min(m_count, std::size_t(std::ceil((top + inner.height()) / rowHeight)) + 1);
    m_items.clear();
    window.pushContentMask(inner);
    const bool pushText = !m_style.text.isEmpty();
    if (pushText)
        window.pushTextStyle(m_style.text);
    // The rows' scroll container: a focused field in a row reveals its caret through it
    // (Window::revealRect), a finger drags it.
    window.pushScrollFrame(st, inner, false, true, m_hitbox);
    for (std::size_t i = m_first; i < last; ++i) {
        AnyElement row = m_render(i, window, app);
        row.setSiblingIndex(std::uint32_t(i));
        row.layoutAsRoot({ inner.width(), rowHeight }, window, app);
        row.prepaintAt({ inner.left(), inner.top() + float(i) * rowHeight - top }, window, app);
        m_items.push_back(std::move(row));
    }
    m_kept = AnyElement();
    if (const std::optional<std::size_t> kept = m_focusState->row;
        kept && window.focusedId() != 0 && *kept < m_count && (*kept < m_first || *kept >= last)) {
        m_keptIndex = *kept;
        m_kept = m_render(m_keptIndex, window, app);
        m_kept.setSiblingIndex(std::uint32_t(m_keptIndex));
        m_kept.layoutAsRoot({inner.width(), rowHeight}, window, app);
        m_kept.prepaintAt({inner.left(), inner.top() + float(m_keptIndex) * rowHeight - top}, window, app);
    }
    window.popScrollFrame();
    if (pushText)
        window.popTextStyle();
    window.popContentMask();
}

namespace detail {

void paintListRows(const ListRowsPaint &list, Window &window, App &app)
{
    const Rect bounds = list.bounds;
    const Style &style = list.style;
    window.paintQuad(bounds, style.background, style.layout.border, style.borderColor, style.cornerRadii);
    const Rect inner = bounds.inset(style.layout.padding).inset(style.layout.border);
    ScrollState *const scroll = list.scroll;
    AccessibilityNode *node =
            window.accessibilityEnabled() ? window.beginAccessibilityNode(Role::List, bounds) : nullptr;
    if (node) {
        node->name = list.label;
        node->rowCount = std::int64_t(list.count);
        if (scroll) {
            node->scrolls = true;
            node->scrollY = true;
            node->scrollState = scroll;
            node->scrollOffset = scroll->offset;
            node->scrollContent = scroll->content;
            node->scrollViewport = inner;
            if (scroll->maxOffset().y > 0)
                node->actions |=
                        actionBit(AccessibilityAction::ScrollForward) | actionBit(AccessibilityAction::ScrollBackward);
        }
    }
    window.pushContentMask(inner);
    std::optional<std::size_t> focusedRow;
    auto paintRow = [&](AnyElement &row, std::size_t index) {
        // Each row is a list item known by its index; the row's root element describes it. A row
        // whose root is hidden is none (another tree has it: a hosted Qt Quick item over it).
        AccessibilityNode *item = nullptr;
        if (node && !(row.element() && row.element()->accessibilityHidden())) {
            item = window.beginAccessibilityNode(Role::ListItem, row.bounds(), hashCombine(0x726f77ull, index) | 1);
            if (item) {
                item->rowIndex = std::int64_t(index);
                item->adoptable = true;
            }
        }
        const std::size_t mark = window.dispatchNodeMark();
        row.paint(window, app);
        if (window.focusPaintedSince(mark))
            focusedRow = index;
        if (item)
            window.endAccessibilityNode();
    };
    if (list.header && !list.header->isEmpty())
        list.header->paint(window, app);
    for (std::size_t k = 0; k < list.rows.size(); ++k)
        paintRow(list.rows[k], list.indices ? list.indices[k] : list.first + k);
    if (list.kept && !list.kept->isEmpty())
        paintRow(*list.kept, list.keptIndex);
    if (list.footer && !list.footer->isEmpty())
        list.footer->paint(window, app);
    if (list.focusedRow)
        *list.focusedRow = focusedRow;
    window.popContentMask();
    if (node)
        window.endAccessibilityNode();

    if (scroll && scroll->content.height > scroll->viewport.height + 0.5f) {
        const ScrollState &st = *scroll;
        const float track = inner.height() - 4;
        const float h = std::max(24.0f, track * st.viewport.height / st.content.height);
        const float max = st.maxOffset().y;
        const float y = inner.top() + 2 + (max > 0 ? (track - h) * st.offset.y / max : 0);
        window.paintQuad(Rect::fromXYWH(inner.right() - 8, y, 6, h), Background::solid(rgba(0x8080808c)), {},
                transparent(), Corners::all(3));
    }

    const HitboxId hitbox = list.hitbox;
    window.onMouseEvent([scroll, hitbox](const PlatformInput &input, DispatchPhase phase, Window &w, App &) {
        const auto *wheel = std::get_if<ScrollWheelEvent>(&input);
        if (!wheel || phase != DispatchPhase::Bubble || !scroll || !w.isHovered(hitbox))
            return;
        if (scroll->scrollBy({ 0, wheel->delta.y })) {
            w.refresh();
            w.stopPropagation();
        }
    });
}

} // namespace detail

void UniformList::paint(Rect bounds, Window &window, App &app)
{
    detail::paintListRows({bounds,
                           m_style,
                           {m_label.data(), m_label.size()},
                           m_count,
                           m_scroll,
                           m_hitbox,
                           std::span<AnyElement>(m_items.data(), m_items.size()),
                           m_first,
                           nullptr,
                           &m_kept,
                           m_keptIndex,
                           m_focusState ? &m_focusState->row : nullptr,
                           nullptr,
                           nullptr},
                          window, app);
}

Rect Img::fitRect(Size image, Rect bounds, ObjectFit fit)
{
    if (image.isEmpty() || bounds.isEmpty() || fit == ObjectFit::Fill)
        return bounds;
    const float sx = bounds.width() / image.width;
    const float sy = bounds.height() / image.height;
    float s = fit == ObjectFit::Cover ? std::max(sx, sy) : std::min(sx, sy);
    if (fit == ObjectFit::ScaleDown)
        s = std::min(s, 1.0f);
    const Size size = image * s;
    return Rect { { bounds.left() + (bounds.width() - size.width) / 2, bounds.top() + (bounds.height() - size.height) / 2 },
        size };
}

NodeId Img::requestLayout(Window &window, App &)
{
    const Size natural = m_image ? Size { float(m_image->width), float(m_image->height) } : Size {};
    return window.requestMeasuredLayout(m_style.layout,
            [natural](std::optional<float> knownWidth, std::optional<float> knownHeight, AvailableSpace, AvailableSpace) {
                // One side known: the other from the picture's aspect ratio.
                if (knownWidth && knownHeight)
                    return Size { *knownWidth, *knownHeight };
                if (natural.isEmpty())
                    return Size { knownWidth.value_or(0), knownHeight.value_or(0) };
                if (knownWidth)
                    return Size { *knownWidth, *knownWidth * natural.height / natural.width };
                if (knownHeight)
                    return Size { *knownHeight * natural.width / natural.height, *knownHeight };
                return natural;
            });
}

void Img::paint(Rect bounds, Window &window, App &)
{
    if (!m_alt.empty() && window.accessibilityEnabled()) {
        if (AccessibilityNode *node = window.beginAccessibilityNode(Role::Image, bounds)) {
            node->name = {m_alt.data(), m_alt.size()};
            window.endAccessibilityNode();
        }
    }
    if (!m_image)
        return;
    const bool fade = m_style.opacity < 1;
    if (fade)
        window.pushOpacity(m_style.opacity);
    window.paintQuad(bounds, m_style.background, m_style.layout.border, m_style.borderColor, m_style.cornerRadii);
    const Rect target = fitRect({ float(m_image->width), float(m_image->height) }, bounds, m_fit);
    // Cover crops: the bounds clip; the corners belong to the bounds, not to the larger picture.
    window.pushContentMask(bounds);
    window.paintPicture(target, m_fit == ObjectFit::Cover ? Corners {} : m_style.cornerRadii, m_image, 1, m_grayscale);
    window.popContentMask();
    if (fade)
        window.popOpacity();
}

} // namespace brulion::atrament
