#pragma once

// div(): the box every UI is built from (GPUI's Div) — a flex container with a background,
// border, corners and shadows, children, and interactivity: hover / active / focus styles,
// clicks, mouse, scroll wheel and key listeners, actions, focus tracking, scrolling (overflow
// scroll, the offset kept as element state), a cursor.
//
//   div().id(u"save").px(12).py(6).rounded(8).bg(theme.accent).cursorPointer()
//        .hover([](Style &s) { s.background = Background::solid(theme.accentHover); })
//        .onClick(cx.listener([](Editor &e, const ClickEvent &, Window &, Context<Editor> &cx) { e.save(cx); }))
//        .child(u"Zapisz")

#include "base/function.h"
#include "ui/app.h"
#include "ui/element.h"
#include "ui/events.h"
#include "ui/style.h"
#include "ui/window.h"

#include <string>
#include <string_view>
#include <utility>

namespace brulion::atrament {

// Kept per scrolling element; a ScrollHandle (below) shares it with code outside the element.
struct ScrollState {
    Point offset; // ≥ 0: how far the content is scrolled
    Size viewport; // the element's padding box
    Size content; // its content
    Point maxOffset() const
    {
        return { std::max(0.0f, content.width - viewport.width), std::max(0.0f, content.height - viewport.height) };
    }
    // Clamps; returns whether the offset changed.
    bool scrollBy(Point delta);
};

class ScrollHandle {
public:
    ScrollHandle()
        : m_state(std::make_shared<ScrollState>())
    {
    }
    Point offset() const { return m_state->offset; }
    void setOffset(Point p)
    {
        m_state->offset = p;
        m_state->scrollBy({});
    }
    const ScrollState &state() const { return *m_state; }
    // Scrolls the least distance that brings `rect` (content coordinates) into view.
    void scrollToReveal(Rect rect);
    std::shared_ptr<ScrollState> shared() const { return m_state; }

private:
    std::shared_ptr<ScrollState> m_state;
};

class Div final : public Element, public Styled<Div> {
public:
    using ClickListener = Fn<void(const ClickEvent &, Window &, App &)>;
    using MouseDownListener = Fn<void(const MouseDownEvent &, Window &, App &)>;
    using MouseUpListener = Fn<void(const MouseUpEvent &, Window &, App &)>;
    using MouseMoveListener = Fn<void(const MouseMoveEvent &, Window &, App &)>;
    using ScrollListener = Fn<void(const ScrollWheelEvent &, Window &, App &)>;
    using KeyDownListener = Fn<void(const KeyDownEvent &, Window &, App &)>;
    using KeyUpListener = Fn<void(const KeyUpEvent &, Window &, App &)>;
    using ActionListener = Fn<void(Window &, App &)>;
    using StyleRefinement = Fn<void(Style &)>;
    using HoverListener = Fn<void(bool hovered, Window &, App &)>;

    Div();

    Style &styleRef() { return m_style; }
    const Style &style() const { return m_style; }

    Div &&id(ElementId id) &&
    {
        m_id = id;
        return std::move(*this);
    }

    template<IntoElement E>
    Div &&child(E &&element) &&
    {
        AnyElement e(std::forward<E>(element));
        e.setSiblingIndex(std::uint32_t(m_children.size()));
        m_children.push_back(std::move(e));
        return std::move(*this);
    }
    // Any range of things that convert to elements.
    template<class R>
    Div &&children(R &&range) &&
    {
        for (auto &&item : range) {
            AnyElement e(std::move(item));
            e.setSiblingIndex(std::uint32_t(m_children.size()));
            m_children.push_back(std::move(e));
        }
        return std::move(*this);
    }

    Div &&onClick(ClickListener f) &&;
    Div &&onMouseDown(MouseButton button, MouseDownListener f) &&;
    Div &&onMouseUp(MouseButton button, MouseUpListener f) &&;
    Div &&onMouseMove(MouseMoveListener f) &&;
    Div &&onScrollWheel(ScrollListener f) &&;
    Div &&onHover(HoverListener f) &&;
    Div &&onKeyDown(KeyDownListener f) &&;
    Div &&onKeyUp(KeyUpListener f) &&;
    Div &&onAction(std::u16string_view action, ActionListener f) &&;
    // The style while hovered / pressed / focused (applied over the base style when painting).
    Div &&hover(StyleRefinement f) &&;
    Div &&active(StyleRefinement f) &&;
    Div &&focused(StyleRefinement f) &&;
    Div &&trackFocus(const FocusHandle &handle) &&;
    Div &&keyContext(std::u16string_view context) &&;
    // Nothing below this element receives the mouse while it is under the pointer.
    Div &&blockMouse() &&;
    // Shares the scroll position with a handle (and keeps it without an id).
    Div &&trackScroll(const ScrollHandle &handle) &&;
    // No scroll bar is drawn for overflow scroll.
    Div &&hideScrollbar() &&;

    NodeId requestLayout(Window &window, App &app) override;
    void prepaint(Rect bounds, Window &window, App &app) override;
    void paint(Rect bounds, Window &window, App &app) override;
    ElementId id() const override { return m_id; }

private:
    struct MouseDownEntry {
        MouseButton button;
        MouseDownListener f;
    };
    struct MouseUpEntry {
        MouseButton button;
        MouseUpListener f;
    };
    struct ActionEntry {
        std::u16string_view action; // in the frame arena
        ActionListener f;
    };
    // Kept across frames for an interactive element.
    struct InteractState {
        bool pressed = false;
        MouseDownEvent down;
        bool hovered = false;
    };
    bool isScrollable() const;
    bool needsHitbox() const;
    void paintScrollbars(Rect bounds, Window &window);
    void registerMouseListeners(Window &window);
    void registerKeyListeners(Window &window);
    std::u16string_view arenaString(std::u16string_view s);

    Style m_style;
    ElementId m_id;
    ArenaVector<AnyElement> m_children;
    ArenaVector<ClickListener> m_click;
    ArenaVector<MouseDownEntry> m_mouseDown;
    ArenaVector<MouseUpEntry> m_mouseUp;
    ArenaVector<MouseMoveListener> m_mouseMove;
    ArenaVector<ScrollListener> m_scroll;
    ArenaVector<HoverListener> m_hoverListeners;
    ArenaVector<KeyDownListener> m_keyDown;
    ArenaVector<KeyUpListener> m_keyUp;
    ArenaVector<ActionEntry> m_actions;
    StyleRefinement m_hoverStyle;
    StyleRefinement m_activeStyle;
    StyleRefinement m_focusStyle;
    FocusHandle m_focus;
    std::u16string_view m_keyContext;
    std::shared_ptr<ScrollState> m_scrollHandle;
    bool m_blockMouse = false;
    bool m_hideScrollbar = false;

    // Set during prepaint, used in paint.
    NodeId m_layout = kNoNode;
    HitboxId m_hitbox = 0;
    InteractState *m_state = nullptr;
    ScrollState *m_scrollState = nullptr;
};

inline Div div()
{
    return Div();
}

// A custom painter: `canvas([](Rect bounds, Window &w, App &) { w.paintPath(…); })`, sized by its
// style (w/h/flex).
class Canvas final : public Element, public Styled<Canvas> {
public:
    using Painter = Fn<void(Rect, Window &, App &)>;
    explicit Canvas(Painter painter)
        : m_painter(painter)
    {
    }
    Style &styleRef() { return m_style; }
    NodeId requestLayout(Window &window, App &app) override;
    void paint(Rect bounds, Window &window, App &app) override;

private:
    Style m_style;
    Painter m_painter;
};

inline Canvas canvas(Canvas::Painter painter)
{
    return Canvas(painter);
}

} // namespace brulion::atrament
