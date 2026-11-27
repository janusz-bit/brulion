#include "ui/div.h"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace brulion::atrament {

bool ScrollState::scrollBy(Point delta)
{
    const Point old = offset;
    const Point m = maxOffset();
    offset.x = std::clamp(offset.x + delta.x, 0.0f, m.x);
    offset.y = std::clamp(offset.y + delta.y, 0.0f, m.y);
    return !(old == offset);
}

void ScrollHandle::scrollToReveal(Rect rect)
{
    ScrollState &s = *m_state;
    Point o = s.offset;
    if (rect.top() < o.y)
        o.y = rect.top();
    else if (rect.bottom() > o.y + s.viewport.height)
        o.y = rect.bottom() - s.viewport.height;
    if (rect.left() < o.x)
        o.x = rect.left();
    else if (rect.right() > o.x + s.viewport.width)
        o.x = rect.right() - s.viewport.width;
    s.offset = o;
    s.scrollBy({});
}

Div::Div() = default;

std::u16string_view Div::arenaString(std::u16string_view s)
{
    Arena *arena = Arena::current();
    if (!arena || s.empty())
        return {};
    auto *p = static_cast<char16_t *>(arena->allocate(s.size() * sizeof(char16_t), alignof(char16_t)));
    std::memcpy(p, s.data(), s.size() * sizeof(char16_t));
    return { p, s.size() };
}

Div &&Div::onClick(ClickListener f) &&
{
    m_click.push_back(f);
    return std::move(*this);
}

Div &&Div::onMouseDown(MouseButton button, MouseDownListener f) &&
{
    m_mouseDown.push_back(MouseDownEntry { button, f });
    return std::move(*this);
}

Div &&Div::onMouseUp(MouseButton button, MouseUpListener f) &&
{
    m_mouseUp.push_back(MouseUpEntry { button, f });
    return std::move(*this);
}

Div &&Div::onMouseMove(MouseMoveListener f) &&
{
    m_mouseMove.push_back(f);
    return std::move(*this);
}

Div &&Div::onScrollWheel(ScrollListener f) &&
{
    m_scroll.push_back(f);
    return std::move(*this);
}

Div &&Div::onHover(HoverListener f) &&
{
    m_hoverListeners.push_back(f);
    return std::move(*this);
}

Div &&Div::onLongPress(LongPressListener f) &&
{
    m_longPress.push_back(f);
    return std::move(*this);
}

Div &&Div::onKeyDown(KeyDownListener f) &&
{
    m_keyDown.push_back(f);
    return std::move(*this);
}

Div &&Div::captureKeyDown(KeyDownListener f) &&
{
    m_captureKeyDown.push_back(f);
    return std::move(*this);
}

Div &&Div::onKeyUp(KeyUpListener f) &&
{
    m_keyUp.push_back(f);
    return std::move(*this);
}

Div &&Div::onAction(std::u16string_view action, ActionListener f) &&
{
    m_actions.push_back(ActionEntry { arenaString(action), f });
    return std::move(*this);
}

Div &&Div::hover(StyleRefinement f) &&
{
    m_hoverStyle = f;
    return std::move(*this);
}

Div &&Div::active(StyleRefinement f) &&
{
    m_activeStyle = f;
    return std::move(*this);
}

Div &&Div::focused(StyleRefinement f) &&
{
    m_focusStyle = f;
    return std::move(*this);
}

Div &&Div::trackFocus(const FocusHandle &handle) &&
{
    m_focus = handle;
    return std::move(*this);
}

Div &&Div::keyContext(std::u16string_view context) &&
{
    m_keyContext = arenaString(context);
    return std::move(*this);
}

Div &&Div::blockMouse() &&
{
    m_blockMouse = true;
    return std::move(*this);
}

Div &&Div::trackScroll(const ScrollHandle &handle) &&
{
    m_scrollHandle = handle.shared();
    return std::move(*this);
}

Div &&Div::hideScrollbar() &&
{
    m_hideScrollbar = true;
    return std::move(*this);
}

Div::AccessibilityProps &Div::access()
{
    if (!m_access) {
        Arena *arena = Arena::current();
        assert(arena && "accessibility builders need a frame arena (Arena::current())");
        static AccessibilityProps outsideFrame; // release builds without a frame: kept, not used
        m_access = arena ? arena->make<AccessibilityProps>() : &outsideFrame;
    }
    return *m_access;
}

Div &&Div::role(Role role) &&
{
    access().role = role;
    access().hasRole = true;
    return std::move(*this);
}

Div &&Div::ariaLabel(std::u16string_view name) &&
{
    access().label = arenaString(name);
    return std::move(*this);
}

Div &&Div::ariaDescription(std::u16string_view description) &&
{
    access().description = arenaString(description);
    return std::move(*this);
}

Div &&Div::ariaValue(std::u16string_view value) &&
{
    access().value = arenaString(value);
    return std::move(*this);
}

Div &&Div::ariaLevel(int level) &&
{
    access().level = level;
    return std::move(*this);
}

Div &&Div::ariaHidden(bool hidden) &&
{
    access().hidden = hidden;
    return std::move(*this);
}

Div &&Div::checked(bool checked) &&
{
    access().checked = checked;
    return std::move(*this);
}

Div &&Div::selected(bool selected) &&
{
    access().selected = selected;
    return std::move(*this);
}

Div &&Div::expanded(bool expanded) &&
{
    access().expanded = expanded;
    return std::move(*this);
}

Div &&Div::disabled(bool disabled) &&
{
    access().disabled = disabled;
    return std::move(*this);
}

Div &&Div::rangeValue(float value, float min, float max, float step) &&
{
    access().range = AccessibilityRange{value, min, max, step};
    return std::move(*this);
}

Div &&Div::onAccessibilityAction(AccessibilityAction action, AccessibilityListener f) &&
{
    access().actions.push_back(AccessibilityActionEntry{action, f});
    return std::move(*this);
}

Role Div::accessibilityRole() const
{
    const AccessibilityProps *a = m_access;
    if (a && a->hasRole)
        return a->role;
    if (a && a->checked)
        return Role::CheckBox;
    if (!m_click.empty() && m_id.isValid())
        return Role::Button;
    if (isScrollable())
        return Role::ScrollView;
    if (m_focus)
        return Role::Group;
    if (a && (!a->label.empty() || !a->description.empty() || !a->value.empty() || !a->actions.empty() || a->range))
        return Role::Group;
    return Role::None;
}

AccessibilityNode *Div::describeAccessibility(Rect bounds, Window &window, bool &ownNode)
{
    // A list's row: the row's root element describes the list item instead of making a node in it.
    const bool explicitRole = m_access && m_access->hasRole;
    AccessibilityNode *node = explicitRole ? nullptr : window.adoptAccessibilityNode();
    if (!node) {
        const Role role = accessibilityRole();
        if (role == Role::None)
            return nullptr;
        node = window.beginAccessibilityNode(role, bounds);
        if (!node)
            return nullptr;
        ownNode = true;
    }
    AccessibilityStates &st = node->states;
    if (const AccessibilityProps *a = m_access) {
        if (!a->label.empty()) {
            node->name = a->label;
            node->nameFromContent = false;
        }
        if (!a->description.empty())
            node->description = a->description;
        if (!a->value.empty())
            node->value = a->value;
        if (a->level > 0)
            node->level = a->level;
        if (a->checked) {
            st.checkable = true;
            st.checked = *a->checked;
        }
        if (a->selected) {
            st.selectable = true;
            st.selected = *a->selected;
        }
        if (a->expanded) {
            st.expandable = true;
            st.expanded = *a->expanded;
        }
        st.disabled = st.disabled || a->disabled;
        if (a->range) {
            node->hasRange = true;
            node->range = *a->range;
        }
        for (const AccessibilityActionEntry &e : a->actions)
            node->actions |= actionBit(e.action);
    }
    if (m_focus) {
        st.focusable = true;
        st.focused = st.focused || m_focus.isFocused(window);
        node->actions |= actionBit(AccessibilityAction::Focus);
    }
    if (!m_click.empty() && !st.disabled)
        node->actions |= actionBit(AccessibilityAction::Click);
    if (m_scrollState) {
        const ScrollState &scroll = *m_scrollState;
        node->scrolls = true;
        node->scrollX = m_style.layout.overflowX == Overflow::Scroll;
        node->scrollY = m_style.layout.overflowY == Overflow::Scroll;
        node->scrollState = m_scrollState;
        node->scrollOffset = scroll.offset;
        node->scrollContent = scroll.content;
        node->scrollViewport = bounds.inset(m_style.layout.border);
        const Point max = scroll.maxOffset();
        if ((node->scrollY && max.y > 0) || (node->scrollX && max.x > 0))
            node->actions |=
                    actionBit(AccessibilityAction::ScrollForward) | actionBit(AccessibilityAction::ScrollBackward);
    }
    Div *self = this; // in the frame arena, as the tree is
    node->handler = [self](const AccessibilityActionRequest &r, Window &w, App &app) {
        return self->performAccessibilityAction(r, w, app);
    };
    return node;
}

bool Div::performAccessibilityAction(const AccessibilityActionRequest &request, Window &window, App &app)
{
    if (m_access) {
        for (const AccessibilityActionEntry &e : m_access->actions) {
            if (e.action == request.action) {
                e.f(request, window, app);
                return true;
            }
        }
    }
    switch (request.action) {
    case AccessibilityAction::Click: {
        if (m_click.empty() || isDisabled())
            return false;
        ClickEvent click;
        click.fromAccessibility = true;
        for (const ClickListener &l : m_click)
            l(click, window, app);
        return true;
    }
    case AccessibilityAction::Focus:
        if (!m_focus)
            return false;
        window.focus(m_focus);
        return true;
    default: return false;
    }
}

bool Div::isScrollable() const
{
    return m_style.layout.overflowX == Overflow::Scroll || m_style.layout.overflowY == Overflow::Scroll;
}

bool Div::needsHitbox() const
{
    return m_blockMouse || m_focus || isScrollable() || m_style.cursor || m_hoverStyle || m_activeStyle
            || !m_click.empty() || !m_mouseDown.empty() || !m_mouseUp.empty() || !m_mouseMove.empty()
            || !m_scroll.empty() || !m_hoverListeners.empty() || !m_longPress.empty();
}

NodeId Div::requestLayout(Window &window, App &app)
{
    // Interaction state of the last frame refines the style before anything is laid out, so a
    // hover may change a text colour or a size (as in GPUI).
    if (m_hoverStyle || m_activeStyle || !m_click.empty() || !m_hoverListeners.empty() || !m_mouseMove.empty()) {
        m_state = &window.elementState<InteractState>();
        if (m_hoverStyle && m_state->hovered)
            m_hoverStyle(m_style);
        if (m_activeStyle && m_state->pressed)
            m_activeStyle(m_style);
    }
    if (m_focusStyle && m_focus.isFocused(window))
        m_focusStyle(m_style);

    const bool pushText = !m_style.text.isEmpty();
    if (pushText)
        window.pushTextStyle(m_style.text);
    ArenaVector<NodeId> ids;
    ids.reserve(m_children.size());
    for (AnyElement &c : m_children)
        ids.push_back(c.requestLayout(window, app));
    if (pushText)
        window.popTextStyle();
    m_layout = window.requestLayout(m_style.layout, ids);
    return m_layout;
}

void Div::prepaint(Rect bounds, Window &window, App &app)
{
    if (m_style.layout.display == Display::None)
        return;
    if (m_state)
        (void)window.elementState<InteractState>(); // keep it alive
    if (needsHitbox())
        m_hitbox = window.insertHitbox(bounds, m_blockMouse ? HitboxBehavior::BlockMouse : HitboxBehavior::Normal);

    Point scroll;
    if (isScrollable()) {
        ScrollState &st = m_scrollHandle ? *m_scrollHandle : window.elementState<ScrollState>();
        m_scrollState = &st;
        const Rect inner = bounds.inset(m_style.layout.border);
        st.viewport = inner.size;
        st.content = window.nodeLayout(m_layout).contentSize;
        st.scrollBy({});
        if (m_style.layout.overflowX == Overflow::Scroll)
            scroll.x = st.offset.x;
        if (m_style.layout.overflowY == Overflow::Scroll)
            scroll.y = st.offset.y;
    }
    const bool clip = m_style.layout.overflowX != Overflow::Visible || m_style.layout.overflowY != Overflow::Visible;
    if (clip)
        window.pushContentMask(bounds.inset(m_style.layout.border));
    // The text style holds in every phase: a list renders its rows in prepaint.
    const bool pushText = !m_style.text.isEmpty();
    if (pushText)
        window.pushTextStyle(m_style.text);
    window.pushElementOffset(-scroll);
    if (m_scrollState)
        window.pushScrollFrame(*m_scrollState, bounds.inset(m_style.layout.border),
                               m_style.layout.overflowX == Overflow::Scroll,
                               m_style.layout.overflowY == Overflow::Scroll, m_hitbox);
    for (AnyElement &c : m_children)
        c.prepaint(window, app);
    if (m_scrollState)
        window.popScrollFrame();
    window.popElementOffset();
    if (pushText)
        window.popTextStyle();
    if (clip)
        window.popContentMask();
}

void Div::paint(Rect bounds, Window &window, App &app)
{
    if (m_style.layout.display == Display::None)
        return;
    const Style &s = m_style;
    const bool fade = s.opacity < 1;
    if (fade)
        window.pushOpacity(s.opacity);
    if (s.shadowCount)
        window.paintShadows(bounds, s.cornerRadii, std::span<const BoxShadow>(s.shadows.data(), s.shadowCount));
    window.paintQuad(bounds, s.background, s.layout.border, s.borderColor, s.cornerRadii);
    if (s.cursor && m_hitbox)
        window.setCursorStyle(m_hitbox, *s.cursor);

    const bool node = bool(m_focus) || !m_keyContext.empty() || !m_keyDown.empty() || !m_keyUp.empty()
            || !m_captureKeyDown.empty() || !m_actions.empty();
    if (node)
        window.pushDispatchNode(m_focus.id(), m_keyContext);
    registerMouseListeners(window);
    registerKeyListeners(window);
    bool ownNode = false;
    const bool hidden = m_access && m_access->hidden && window.accessibilityEnabled();
    if (hidden)
        window.pushAccessibilityHidden();
    else if (window.accessibilityEnabled())
        describeAccessibility(bounds, window, ownNode);

    const bool clip = s.layout.overflowX != Overflow::Visible || s.layout.overflowY != Overflow::Visible;
    if (clip)
        window.pushContentMask(bounds.inset(s.layout.border));
    const bool pushText = !s.text.isEmpty();
    if (pushText)
        window.pushTextStyle(s.text);
    for (AnyElement &c : m_children)
        c.paint(window, app);
    if (pushText)
        window.popTextStyle();
    if (clip)
        window.popContentMask();
    if (ownNode)
        window.endAccessibilityNode();
    if (hidden)
        window.popAccessibilityHidden();
    if (m_scrollState && !m_hideScrollbar)
        paintScrollbars(bounds, window);
    if (node)
        window.popDispatchNode();
    if (fade)
        window.popOpacity();
}

void Div::paintScrollbars(Rect bounds, Window &window)
{
    const ScrollState &st = *m_scrollState;
    const Rect inner = bounds.inset(m_style.layout.border);
    const Color thumb = rgba(0x8080808c);
    constexpr float thickness = 6;
    constexpr float inset = 2;
    if (m_style.layout.overflowY == Overflow::Scroll && st.content.height > st.viewport.height + 0.5f) {
        const float track = inner.height() - 2 * inset;
        const float h = std::max(24.0f, track * st.viewport.height / st.content.height);
        const float max = st.maxOffset().y;
        const float y = inner.top() + inset + (max > 0 ? (track - h) * st.offset.y / max : 0);
        window.paintQuad(Rect::fromXYWH(inner.right() - thickness - inset, y, thickness, h), Background::solid(thumb),
                {}, transparent(), Corners::all(thickness / 2));
    }
    if (m_style.layout.overflowX == Overflow::Scroll && st.content.width > st.viewport.width + 0.5f) {
        const float track = inner.width() - 2 * inset;
        const float w = std::max(24.0f, track * st.viewport.width / st.content.width);
        const float max = st.maxOffset().x;
        const float x = inner.left() + inset + (max > 0 ? (track - w) * st.offset.x / max : 0);
        window.paintQuad(Rect::fromXYWH(x, inner.bottom() - thickness - inset, w, thickness), Background::solid(thumb),
                {}, transparent(), Corners::all(thickness / 2));
    }
}

void Div::registerMouseListeners(Window &window)
{
    if (!m_hitbox)
        return;
    const HitboxId hitbox = m_hitbox;
    // `this` lives in the frame's arena exactly as long as the listeners registered here.
    Div *self = this;

    if (m_state && (m_hoverStyle || !m_hoverListeners.empty())) {
        window.onMouseEvent([self, hitbox](const PlatformInput &input, DispatchPhase phase, Window &w, App &app) {
            if (phase != DispatchPhase::Capture || std::holds_alternative<KeyDownEvent>(input))
                return;
            const bool now = w.isHovered(hitbox);
            if (now == self->m_state->hovered)
                return;
            self->m_state->hovered = now;
            if (self->m_hoverStyle)
                w.refresh();
            for (const HoverListener &l : self->m_hoverListeners)
                l(now, w, app);
        });
    }

    for (const LongPressListener &l : m_longPress)
        window.onLongPress(hitbox, l);

    if (!m_mouseDown.empty() || m_focus || m_state) {
        window.onMouseEvent([self, hitbox](const PlatformInput &input, DispatchPhase phase, Window &w, App &app) {
            const auto *down = std::get_if<MouseDownEvent>(&input);
            if (!down || phase != DispatchPhase::Bubble || !w.isHovered(hitbox))
                return;
            if (self->m_focus && !w.defaultPrevented()) {
                w.focus(self->m_focus);
                w.preventDefault();
            }
            if (self->m_state && down->button == MouseButton::Left) {
                self->m_state->pressed = true;
                self->m_state->down = *down;
                if (self->m_activeStyle)
                    w.refresh();
            }
            for (const MouseDownEntry &e : self->m_mouseDown) {
                if (e.button == down->button)
                    e.f(*down, w, app);
            }
        });
    }

    if (!m_mouseUp.empty() || m_state) {
        window.onMouseEvent([self, hitbox](const PlatformInput &input, DispatchPhase phase, Window &w, App &app) {
            const auto *up = std::get_if<MouseUpEvent>(&input);
            if (!up)
                return;
            InteractState *st = self->m_state;
            if (phase == DispatchPhase::Capture) {
                // Released elsewhere: no click, and the pressed look goes.
                if (st && st->pressed && !w.isHovered(hitbox)) {
                    st->pressed = false;
                    if (self->m_activeStyle)
                        w.refresh();
                }
                return;
            }
            if (!w.isHovered(hitbox))
                return;
            for (const MouseUpEntry &e : self->m_mouseUp) {
                if (e.button == up->button)
                    e.f(*up, w, app);
            }
            if (st && st->pressed && up->button == MouseButton::Left) {
                st->pressed = false;
                if (self->m_activeStyle)
                    w.refresh();
                if (!self->m_click.empty() && !self->isDisabled()) {
                    ClickEvent click;
                    click.down = st->down;
                    click.up = *up;
                    for (const ClickListener &l : self->m_click)
                        l(click, w, app);
                    w.stopPropagation(); // the innermost clickable element takes the click
                }
            }
        });
    }

    if (!m_mouseMove.empty()) {
        window.onMouseEvent([self, hitbox](const PlatformInput &input, DispatchPhase phase, Window &w, App &app) {
            const auto *move = std::get_if<MouseMoveEvent>(&input);
            if (!move || phase != DispatchPhase::Bubble)
                return;
            // A drag that started here follows the pointer outside too.
            if (w.isHovered(hitbox) || (self->m_state && self->m_state->pressed))
                for (const MouseMoveListener &l : self->m_mouseMove)
                    l(*move, w, app);
        });
    }

    if (m_scrollState || !m_scroll.empty()) {
        window.onMouseEvent([self, hitbox](const PlatformInput &input, DispatchPhase phase, Window &w, App &app) {
            const auto *wheel = std::get_if<ScrollWheelEvent>(&input);
            if (!wheel || phase != DispatchPhase::Bubble || !w.isHovered(hitbox))
                return;
            for (const ScrollListener &l : self->m_scroll)
                l(*wheel, w, app);
            if (!w.propagating() || !self->m_scrollState)
                return;
            Point delta = wheel->delta;
            const LayoutStyle &ls = self->m_style.layout;
            const bool canX = ls.overflowX == Overflow::Scroll;
            const bool canY = ls.overflowY == Overflow::Scroll;
            if (wheel->modifiers.shift && canX && delta.x == 0)
                std::swap(delta.x, delta.y);
            if (!canX)
                delta.x = 0;
            if (!canY)
                delta.y = 0;
            // An inner list at its end lets the wheel scroll the page around it.
            if (self->m_scrollState->scrollBy(delta)) {
                w.refresh();
                w.stopPropagation();
            }
        });
    }
}

void Div::registerKeyListeners(Window &window)
{
    Div *self = this;
    if (!m_captureKeyDown.empty()) {
        window.onKeyEvent([self](const PlatformInput &input, DispatchPhase phase, Window &w, App &app) {
            const auto *down = std::get_if<KeyDownEvent>(&input);
            if (!down || phase != DispatchPhase::Capture)
                return;
            for (const KeyDownListener &l : self->m_captureKeyDown) {
                l(*down, w, app);
                if (!w.propagating())
                    return;
            }
        });
    }
    if (!m_keyDown.empty() || !m_keyUp.empty() || (m_focus && !m_click.empty())) {
        window.onKeyEvent([self](const PlatformInput &input, DispatchPhase phase, Window &w, App &app) {
            if (phase != DispatchPhase::Bubble)
                return;
            if (const auto *down = std::get_if<KeyDownEvent>(&input)) {
                for (const KeyDownListener &l : self->m_keyDown) {
                    l(*down, w, app);
                    if (!w.propagating())
                        return;
                }
                // A focused clickable element is pressed with Enter or Space.
                const Keystroke &k = down->keystroke;
                if (self->m_focus && self->m_focus.isFocused(w) && !self->m_click.empty() && !self->isDisabled()
                    && !k.modifiers.any() && (k.key == u"enter" || k.key == u"space")) {
                    ClickEvent click;
                    click.isKeyboard = true;
                    for (const ClickListener &l : self->m_click)
                        l(click, w, app);
                    w.stopPropagation();
                }
            } else if (const auto *up = std::get_if<KeyUpEvent>(&input)) {
                for (const KeyUpListener &l : self->m_keyUp)
                    l(*up, w, app);
            }
        });
    }
    for (const ActionEntry &a : m_actions)
        window.onAction(a.action, a.f);
}

NodeId Canvas::requestLayout(Window &window, App &)
{
    return window.requestLayout(m_style.layout, {});
}

void Canvas::paint(Rect bounds, Window &window, App &app)
{
    const bool fade = m_style.opacity < 1;
    if (fade)
        window.pushOpacity(m_style.opacity);
    window.paintQuad(bounds, m_style.background, m_style.layout.border, m_style.borderColor, m_style.cornerRadii);
    if (m_painter)
        m_painter(bounds, window, app);
    if (fade)
        window.popOpacity();
}

} // namespace brulion::atrament
