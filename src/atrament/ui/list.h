#pragma once

// Long lists (docs/ATRAMENT.md M1, the journal's blocks, search results, the sidebar's pages):
// only the rows in view exist in a frame. A list of a million rows costs what its visible rows
// cost.
//
//   uniformList(u"results", results.size(), [&](std::size_t i, Window &, App &) -> AnyElement {
//       return div().px(12).h(32).child(results[i].title);
//   }).flex1()
//
// Every row of a uniform list is as tall as `itemHeight()` — or, when not given, as the first row
// measures at the list's width. Rows are identified by their index (ElementId::sibling(i)), so
// their element state (hover, an inner scroll) follows the index. The row with the focus stays in
// the frame (painted outside the view, so invisible) while it is scrolled away: a focused field
// keeps its input method and scrolls its caret back into view (Window::revealRect) when an
// on-screen keyboard covers it.

#include "base/function.h"
#include "ui/div.h"
#include "ui/element.h"
#include "ui/style.h"
#include "ui/text.h"
#include "ui/window.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>

namespace brulion::atrament {

namespace detail {
// What a virtual list paints after its prepaint (uniformList and list() alike): its background,
// the rows in view and the focused row kept outside it, the accessibility list with one item per
// row, the scroll bar, and the wheel.
struct ListRowsPaint {
    Rect bounds;
    const Style &style;
    std::u16string_view label;
    std::size_t count;
    ScrollState *scroll;
    HitboxId hitbox;
    std::span<AnyElement> rows;
    std::size_t first;          // the index of rows[0] when `indices` is null (rows[k] is first + k)
    const std::size_t *indices; // else each row's index
    AnyElement *kept;           // the focused row outside the view, or null / empty
    std::size_t keptIndex;
    std::optional<std::size_t> *focusedRow; // set to the row painted with the focus
    // Painted with the rows, before and after them, as no rows (no list item): a list's header and
    // footer (measuredlist.h), or null / empty.
    AnyElement *header;
    AnyElement *footer;
};
void paintListRows(const ListRowsPaint &list, Window &window, App &app);
} // namespace detail

class UniformList final : public Element, public Styled<UniformList> {
public:
    using RenderItem = Fn<AnyElement(std::size_t index, Window &, App &)>;

    UniformList(ElementId id, std::size_t count, RenderItem render);

    Style &styleRef() { return m_style; }
    UniformList &&itemHeight(float h) &&
    {
        m_itemHeight = h;
        return std::move(*this);
    }
    UniformList &&trackScroll(const ScrollHandle &handle) &&
    {
        m_scrollHandle = handle.shared();
        return std::move(*this);
    }
    // The list's accessible name ("Notatki"). The list is a list node with a row count, each row
    // shown a list item with its index, described by the row's root element.
    UniformList &&ariaLabel(std::u16string_view name) &&;

    NodeId requestLayout(Window &window, App &app) override;
    void prepaint(Rect bounds, Window &window, App &app) override;
    void paint(Rect bounds, Window &window, App &app) override;
    ElementId id() const override { return m_id; }

    // The rows of the last prepaint (for tests): [first, first + count).
    std::size_t firstVisible() const { return m_first; }
    std::size_t visibleCount() const { return m_items.size(); }

private:
    ElementId m_id;
    std::size_t m_count;
    RenderItem m_render;
    Style m_style;
    std::optional<float> m_itemHeight;
    std::shared_ptr<ScrollState> m_scrollHandle;
    ArenaU16String m_label;

    NodeId m_layout = kNoNode;
    HitboxId m_hitbox = 0;
    ScrollState *m_scroll = nullptr;
    std::size_t m_first = 0;
    ArenaVector<AnyElement> m_items;
    struct FocusState;
    FocusState *m_focusState = nullptr;
    AnyElement m_kept; // the focused row outside [m_first, m_first + m_items.size())
    std::size_t m_keptIndex = 0;
};

inline UniformList uniformList(ElementId id, std::size_t count, UniformList::RenderItem render)
{
    return UniformList(id, count, render);
}

enum class ObjectFit : std::uint8_t {
    Fill, // stretched to the bounds
    Contain, // all of it, letterboxed
    Cover, // the bounds filled, cropped
    ScaleDown, // Contain, but never larger than its own size
};

// A picture: `img(image).w(120).h(80).objectFit(ObjectFit::Cover).rounded(8)`. Without a size it
// takes its own (one image pixel per logical pixel).
class Img final : public Element, public Styled<Img> {
public:
    explicit Img(std::shared_ptr<const ImageData> image)
        : m_image(std::move(image))
    {
    }
    Style &styleRef() { return m_style; }
    Img &&objectFit(ObjectFit fit) &&
    {
        m_fit = fit;
        return std::move(*this);
    }
    Img &&grayscale(bool g = true) &&
    {
        m_grayscale = g;
        return std::move(*this);
    }
    // What the picture shows, for a screen reader; without it the picture is decoration.
    Img &&alt(std::u16string_view text) &&
    {
        m_alt.assign(text.begin(), text.end());
        return std::move(*this);
    }
    NodeId requestLayout(Window &window, App &app) override;
    void paint(Rect bounds, Window &window, App &app) override;

    // Where the image goes inside `bounds` for a fit (exposed for tests).
    static Rect fitRect(Size image, Rect bounds, ObjectFit fit);

private:
    std::shared_ptr<const ImageData> m_image;
    Style m_style;
    ObjectFit m_fit = ObjectFit::Contain;
    bool m_grayscale = false;
    ArenaU16String m_alt;
};

inline Img img(std::shared_ptr<const ImageData> image)
{
    return Img(std::move(image));
}

} // namespace brulion::atrament
