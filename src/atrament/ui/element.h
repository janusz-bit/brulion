#pragma once

// Elements (docs/ATRAMENT.md D4, "A frame"): the tree a view's render() returns, rebuilt for
// every frame in the window's frame arena. An element goes through three phases:
//
//   requestLayout   makes its layout node (and its children's), returns its id
//   prepaint        learns its bounds; registers hitboxes, updates element state, lays out text
//   paint           appends primitives to the scene, registers event listeners
//
// `AnyElement` owns a type-erased element in the arena (no heap). Elements are identified across
// frames by their path of ElementIds from the root — an explicit `id()` where the element has one,
// else its index among its siblings — the key of their element state (window.h).

#include "base/arena.h"
#include "base/geometry.h"
#include "base/hash.h"
#include "layout/layout.h"

#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace brulion::atrament {

class App;
class Window;

struct ElementId {
    std::uint64_t value = 0;

    ElementId() = default;
    ElementId(std::u16string_view name)
        : value(hashString(name) | 1)
    {
    }
    ElementId(const char16_t *name)
        : ElementId(std::u16string_view(name))
    {
    }
    ElementId(std::string_view name)
        : value(hashString(name) | 1)
    {
    }
    ElementId(const char *name)
        : ElementId(std::string_view(name))
    {
    }
    static ElementId number(std::uint64_t n)
    {
        ElementId id;
        id.value = hashCombine(0x6e756d626572ull, n) | 1; // never 0
        return id;
    }
    // The id of an element without one of its own: its index among its siblings.
    static ElementId sibling(std::uint64_t index)
    {
        ElementId id;
        id.value = hashCombine(0x7369626c696e67ull, index) | 1;
        return id;
    }
    // A name and a number: ElementId(u"row", 12).
    ElementId(std::u16string_view name, std::uint64_t n)
        : value(hashCombine(hashString(name), n) | 1)
    {
    }
    bool isValid() const { return value != 0; }
    bool operator==(const ElementId &) const = default;
};

class Element {
public:
    virtual ~Element() = default;
    virtual NodeId requestLayout(Window &window, App &app) = 0;
    virtual void prepaint(Rect bounds, Window &window, App &app);
    virtual void paint(Rect bounds, Window &window, App &app) = 0;
    // An explicit identity across frames (lists of rows give their rows ids).
    virtual ElementId id() const { return {}; }
};

class AnyElement {
public:
    AnyElement() = default;
    template<class E, class = std::enable_if_t<std::is_base_of_v<Element, std::decay_t<E>> && !std::is_lvalue_reference_v<E>>>
    AnyElement(E &&element)
        : m_element(allocate<std::decay_t<E>>(std::move(element)))
    {
    }
    // Anything with intoElement() (an Entity<T> of a view, components).
    template<class E>
        requires requires(E &&e) {
            { std::forward<E>(e).intoElement() } -> std::same_as<AnyElement>;
        }
    AnyElement(E &&e)
        : AnyElement(std::forward<E>(e).intoElement())
    {
    }
    // Text: a string literal or a string becomes a text element (ui/text.h).
    AnyElement(const char16_t *text);
    AnyElement(std::u16string text);
    AnyElement(std::u16string_view text);

    bool isEmpty() const { return m_element == nullptr; }
    Element *element() const { return m_element; }

    // Index among its siblings, the part of its path when it has no id (set by the parent).
    void setSiblingIndex(std::uint32_t i) { m_siblingIndex = i; }

    NodeId requestLayout(Window &window, App &app);
    void prepaint(Window &window, App &app);
    void paint(Window &window, App &app);
    NodeId layoutId() const { return m_layout; }
    Rect bounds() const { return m_bounds; }

    // An element outside the parent's layout (an overlay, a list's row): its own layout tree laid
    // out for `available`, then prepainted at `origin` (window logical px, before offsets).
    Size layoutAsRoot(Size available, Window &window, App &app);
    Size layoutAsRoot(AvailableSpace width, AvailableSpace height, Window &window, App &app);
    void prepaintAt(Point origin, Window &window, App &app);

private:
    template<class E, class... A>
    static Element *allocate(A &&...args)
    {
        Arena *arena = Arena::current();
        return arena ? static_cast<Element *>(arena->make<E>(std::forward<A>(args)...)) : nullptr;
    }
    ElementId pathId() const;

    Element *m_element = nullptr;
    NodeId m_layout = kNoNode;
    Rect m_bounds;
    std::uint32_t m_siblingIndex = 0;
};

// Anything that converts to an AnyElement: elements, strings, views (Entity<T> with render()).
template<class T>
concept IntoElement = std::is_constructible_v<AnyElement, T &&>;

// An element that draws nothing and takes no space (for `cond ? x : empty()`).
AnyElement empty();

} // namespace brulion::atrament
