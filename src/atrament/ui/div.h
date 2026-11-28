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
//
// Accessibility (ui/accessibility.h): a div makes a node of the semantic tree when it has a role —
// its own (`role(Role::Heading)`), or one it has by default: a button when it has an id and a click
// listener, a checkbox with checked(), a scroll view when it scrolls, a group when it tracks focus
// or has a label. Text inside names a button (as on the web); `ariaLabel(…)` names it instead.
// disabled() also keeps its click listeners from running.

#include "base/function.h"
#include "ui/app.h"
#include "ui/element.h"
#include "ui/events.h"
#include "ui/style.h"
#include "ui/window.h"

#include <optional>
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
    using AccessibilityListener = Fn<void(const AccessibilityActionRequest &, Window &, App &)>;

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
    // A finger or a pen held still on it for the long-press time (Window::onLongPress): its press is
    // cancelled, no click follows.
    using LongPressListener = Fn<void(Point position, Window &, App &)>;
    Div &&onLongPress(LongPressListener f) &&;
    // A finger's or a pen's press on it is its own drag from the start (Window::claimTouchDrag): its
    // mouse listeners get the press at once and every move, never a scroll of a container around
    // it, no long press — a grip, a thumb, a handle.
    Div &&claimTouchDrag() &&;
    Div &&onKeyDown(KeyDownListener f) &&;
    // A key down seen on its way to the focused element, before anything inside handles it (the
    // capture phase): an editor's keys around a text field (stopPropagation() keeps it from the
    // field). GPUI's capture_key_down.
    Div &&captureKeyDown(KeyDownListener f) &&;
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

    // --- accessibility: what assistive technology is told about this element
    Div &&role(Role role) &&; // Role::None: no node, not even a default one
    Div &&ariaLabel(std::u16string_view name) &&;
    Div &&ariaDescription(std::u16string_view description) &&;
    Div &&ariaValue(std::u16string_view value) &&;
    Div &&ariaLevel(int level) &&; // a heading's 1–6
    // The element and what is inside are left out (decoration, an icon beside its label).
    Div &&ariaHidden(bool hidden = true) &&;
    Div &&checked(bool checked) &&;
    Div &&selected(bool selected = true) &&;
    Div &&expanded(bool expanded) &&;
    Div &&disabled(bool disabled = true) &&;
    Div &&rangeValue(float value, float min, float max, float step = 0) &&;
    // Handles an action of its own (Increment / Decrement of a slider, SetValue…).
    Div &&onAccessibilityAction(AccessibilityAction action, AccessibilityListener f) &&;

    NodeId requestLayout(Window &window, App &app) override;
    void prepaint(Rect bounds, Window &window, App &app) override;
    void paint(Rect bounds, Window &window, App &app) override;
    ElementId id() const override { return m_id; }
    bool accessibilityHidden() const override;

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
    struct AccessibilityActionEntry {
        AccessibilityAction action;
        AccessibilityListener f;
    };
    // In the frame arena, only for a div that was given any of it.
    struct AccessibilityProps {
        Role role = Role::None;
        bool hasRole = false;
        bool hidden = false;
        bool disabled = false;
        int level = 0;
        std::optional<bool> checked;
        std::optional<bool> selected;
        std::optional<bool> expanded;
        std::optional<AccessibilityRange> range;
        std::u16string_view label;
        std::u16string_view description;
        std::u16string_view value;
        ArenaVector<AccessibilityActionEntry> actions;
    };
    // Kept across frames for an interactive element.
    struct InteractState {
        bool pressed = false;
        MouseDownEvent down;
        bool hovered = false;
    };
    // A scroll view's thumbs and their drag with the mouse, kept across frames: where each thumb
    // was painted (none: no thumb on that axis), and the drag going on (axis −1: none).
    // A scroll bar's thumb: where it is painted, and the content px scrolled per px it moves (0
    // when it is as long as its track).
    struct Thumb {
        Rect rect;
        float offsetPerPx = 0;
    };
    static std::optional<Thumb> thumbOn(int axis, Rect inner, const ScrollState &st);
    struct ThumbDrag {
        std::optional<Thumb> thumbs[2]; // x, y: this frame's
        int axis = -1; // the thumb dragged, -1: none
        float startPointer = 0;
        float startOffset = 0;
        float offsetPerPx = 0;
        // A press at `p` on a thumb starts its drag; the pointer at `p` scrolls (whether it moved).
        bool press(Point p, const ScrollState &st);
        bool move(Point p, ScrollState &st) const;
        // A mouse event while the view is shown: not the drag's, taken, taken and scrolled.
        enum class Outcome { NotTaken, Taken, Scrolled };
        Outcome handle(const PlatformInput &input, ScrollState &st);
    };
    bool isScrollable() const;
    bool needsHitbox() const;
    void paintScrollbars(Rect bounds, Window &window);
    void registerThumbDrag(Window &window);
    Point prepaintScroll(Rect bounds, Window &window);
    void prepaintChildren(Rect bounds, Point scroll, Window &window, App &app);
    void registerMouseListeners(Window &window);
    void registerKeyListeners(Window &window);
    std::u16string_view arenaString(std::u16string_view s);
    AccessibilityProps &access();
    bool isDisabled() const { return m_access && m_access->disabled; }
    Role accessibilityRole() const;
    AccessibilityNode *describeAccessibility(Rect bounds, Window &window, bool &ownNode);
    bool performAccessibilityAction(const AccessibilityActionRequest &request, Window &window, App &app);

    Style m_style;
    ElementId m_id;
    ArenaVector<AnyElement> m_children;
    ArenaVector<ClickListener> m_click;
    ArenaVector<MouseDownEntry> m_mouseDown;
    ArenaVector<MouseUpEntry> m_mouseUp;
    ArenaVector<MouseMoveListener> m_mouseMove;
    ArenaVector<ScrollListener> m_scroll;
    ArenaVector<HoverListener> m_hoverListeners;
    ArenaVector<LongPressListener> m_longPress;
    ArenaVector<KeyDownListener> m_keyDown;
    ArenaVector<KeyDownListener> m_captureKeyDown;
    ArenaVector<KeyUpListener> m_keyUp;
    ArenaVector<ActionEntry> m_actions;
    StyleRefinement m_hoverStyle;
    StyleRefinement m_activeStyle;
    StyleRefinement m_focusStyle;
    FocusHandle m_focus;
    std::u16string_view m_keyContext;
    std::shared_ptr<ScrollState> m_scrollHandle;
    AccessibilityProps *m_access = nullptr;
    bool m_blockMouse = false;
    bool m_hideScrollbar = false;
    bool m_claimTouchDrag = false;

    // Set during prepaint, used in paint.
    NodeId m_layout = kNoNode;
    HitboxId m_hitbox = 0;
    InteractState *m_state = nullptr;
    ScrollState *m_scrollState = nullptr;
    ThumbDrag *m_thumbDrag = nullptr;
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
    // `f` runs in prepaint with the bounds too, where the scroll containers around are known (a
    // rectangle revealed: Window::revealRect) and hitboxes go.
    Canvas &&onPrepaint(Painter f) &&
    {
        m_prepaint = f;
        return std::move(*this);
    }
    NodeId requestLayout(Window &window, App &app) override;
    void prepaint(Rect bounds, Window &window, App &app) override;
    void paint(Rect bounds, Window &window, App &app) override;

private:
    Style m_style;
    Painter m_painter;
    Painter m_prepaint;
};

inline Canvas canvas(Canvas::Painter painter)
{
    return Canvas(painter);
}

} // namespace brulion::atrament
