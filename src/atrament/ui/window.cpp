#include "ui/window.h"

#include "text/textsystem.h"

#include <algorithm>
#include <cmath>

namespace brulion::atrament {

void Window::Frame::clear()
{
    // The order matters: listeners and handlers live in the arena.
    scene.clear();
    hitboxes.clear();
    hitTest.clear();
    cursors.clear();
    mouseListeners.clear();
    dispatchNodes.clear();
    keyListeners.clear();
    actionListeners.clear();
    inputFocus = 0;
    inputHandler = nullptr;
    views.clear();
    arena.reset();
}

Window::Window(App &app, PlatformWindow &platform, AnyView root)
    : m_app(app)
    , m_platform(platform)
    , m_root(root)
{
    PlatformWindow::Callbacks callbacks;
    callbacks.frame = [this] { draw(); };
    callbacks.input = [this](const PlatformInput &input) { return dispatchInput(input); };
    callbacks.resized = [this] { refresh(); };
    callbacks.activeChanged = [this](bool) { refresh(); };
    callbacks.inputHandler = [this] { return inputHandler(); };
    m_platform.setCallbacks(std::move(callbacks));
    m_viewport = m_platform.contentSize();
    m_scale = m_platform.scaleFactor();
    refresh();
}

Window::~Window()
{
    m_platform.setCallbacks({});
    // Element states and frames go before the arena's objects they might point into.
    m_states.clear();
    m_frames[0].clear();
    m_frames[1].clear();
}

void Window::refresh()
{
    if (m_refreshRequested && !m_drawing)
        return;
    m_refreshRequested = true;
    m_platform.requestFrame();
}

const Scene &Window::scene() const
{
    return rendered().scene;
}

void Window::recordView(EntityId id)
{
    if (m_building)
        m_building->views.push_back(id);
}

bool Window::drewEntity(EntityId id) const
{
    const auto &views = rendered().views;
    return std::binary_search(views.begin(), views.end(), id);
}

void Window::draw()
{
    if (m_drawing)
        return;
    m_refreshRequested = false;
    m_app.update([&] {
        m_drawing = true;
        const int buildIndex = 1 - m_renderedIndex;
        Frame &frame = m_frames[buildIndex];
        frame.clear();
        m_building = &frame;
        ++m_frameCount;
        m_viewport = m_platform.contentSize();
        m_scale = std::max(0.25f, m_platform.scaleFactor());
        m_modifiers = m_platform.modifiers();
        m_layout.clear();
        m_idStack.assign(1, kHashSeed);
        m_offsets.assign(1, Point {});
        m_masks.assign(1, Rect { {}, m_viewport });
        m_opacity.assign(1, 1.0f);
        m_textStyles.assign(1, TextStyle {});
        m_dispatchStack.clear();
        frame.dispatchNodes.push_back(DispatchNode {});
        m_dispatchStack.push_back(0);
        m_app.atlas().beginFrame();
        {
            ArenaScope scope(frame.arena);
            AnyElement root = ViewElement(m_root);
            if (!root.isEmpty()) {
                root.layoutAsRoot(m_viewport, *this, m_app);
                root.prepaintAt({}, *this, m_app);
                computeHitTest(frame, m_mouse);
                root.paint(*this, m_app);
            }
        }
        std::sort(frame.views.begin(), frame.views.end());
        m_renderedIndex = buildIndex;
        m_building = nullptr;
        m_drawing = false;

        // Element state nobody asked for in this frame is gone with its element.
        for (auto it = m_states.begin(); it != m_states.end();) {
            if (it->second->lastFrame != m_frameCount)
                it = m_states.erase(it);
            else
                ++it;
        }
        m_app.textSystem().endFrame();
        if (m_frameCount % 64 == 0)
            m_app.atlas().collectGarbage(256);

        m_stats.primitives = frame.scene.size();
        m_stats.layoutNodes = m_layout.nodeCount();
        m_stats.hitboxes = frame.hitboxes.size();
        m_stats.arenaBytes = frame.arena.used();
        updateCursor();
        m_platform.present(frame.scene, m_app.atlas());
        m_platform.inputStateChanged();
    });
}

// --- layout

NodeId Window::requestLayout(const LayoutStyle &style, std::span<const NodeId> children)
{
    return m_layout.newNode(style, children);
}

NodeId Window::requestMeasuredLayout(const LayoutStyle &style, MeasureFunction measure)
{
    return m_layout.newLeaf(style, std::move(measure));
}

Rect Window::layoutBounds(NodeId node) const
{
    return m_layout.absolute(node).translated(elementOffset());
}

// --- stacks

void Window::pushElementId(ElementId id)
{
    m_idStack.push_back(hashCombine(m_idStack.back(), id.value));
}

void Window::popElementId()
{
    if (m_idStack.size() > 1)
        m_idStack.pop_back();
}

void Window::pushElementOffset(Point offset)
{
    m_offsets.push_back(m_offsets.back() + offset);
}

void Window::popElementOffset()
{
    if (m_offsets.size() > 1)
        m_offsets.pop_back();
}

void Window::pushContentMask(Rect mask)
{
    m_masks.push_back(m_masks.back().intersected(mask));
}

void Window::popContentMask()
{
    if (m_masks.size() > 1)
        m_masks.pop_back();
}

void Window::pushOpacity(float opacity)
{
    m_opacity.push_back(m_opacity.back() * std::clamp(opacity, 0.0f, 1.0f));
}

void Window::popOpacity()
{
    if (m_opacity.size() > 1)
        m_opacity.pop_back();
}

void Window::pushTextStyle(const TextStyleRefinement &refinement)
{
    TextStyle style = m_textStyles.back();
    refinement.applyTo(style);
    m_textStyles.push_back(std::move(style));
}

void Window::popTextStyle()
{
    if (m_textStyles.size() > 1)
        m_textStyles.pop_back();
}

ContentMask Window::deviceMask() const
{
    return ContentMask { contentMask().scaled(m_scale).roundedOut() };
}

// --- hit testing

HitboxId Window::insertHitbox(Rect bounds, HitboxBehavior behavior)
{
    if (!m_building)
        return 0;
    m_building->hitboxes.push_back(Hitbox { bounds.intersected(contentMask()), behavior });
    return HitboxId(m_building->hitboxes.size());
}

void Window::computeHitTest(Frame &frame, Point position)
{
    frame.hitTest.clear();
    for (std::size_t i = frame.hitboxes.size(); i-- > 0;) {
        const Hitbox &h = frame.hitboxes[i];
        if (h.bounds.contains(position)) {
            frame.hitTest.push_back(HitboxId(i + 1));
            if (h.behavior == HitboxBehavior::BlockMouse)
                break;
        }
    }
}

bool Window::isHovered(HitboxId hitbox) const
{
    if (hitbox == 0)
        return false;
    const Frame &f = m_building ? *m_building : rendered();
    return std::find(f.hitTest.begin(), f.hitTest.end(), hitbox) != f.hitTest.end();
}

Rect Window::hitboxBounds(HitboxId hitbox) const
{
    const Frame &f = m_building ? *m_building : rendered();
    if (hitbox == 0 || hitbox > f.hitboxes.size())
        return {};
    return f.hitboxes[hitbox - 1].bounds;
}

void Window::setCursorStyle(HitboxId hitbox, CursorStyle cursor)
{
    if (m_building && hitbox)
        m_building->cursors.push_back(CursorRequest { hitbox, cursor });
}

void Window::updateCursor()
{
    const Frame &f = rendered();
    CursorStyle cursor = CursorStyle::Arrow;
    bool found = false;
    for (HitboxId h : f.hitTest) {
        for (auto it = f.cursors.rbegin(); it != f.cursors.rend(); ++it) {
            if (it->hitbox == h) {
                cursor = it->cursor;
                found = true;
                break;
            }
        }
        if (found)
            break;
    }
    if (cursor != m_cursor) {
        m_cursor = cursor;
        m_platform.setCursor(cursor);
    }
}

void Window::onMouseEvent(MouseListener listener)
{
    if (m_building)
        m_building->mouseListeners.push_back(listener);
}

// --- key dispatch

void Window::pushDispatchNode(std::uint64_t focusId, std::u16string_view keyContext)
{
    if (!m_building)
        return;
    DispatchNode node;
    node.parent = m_dispatchStack.empty() ? -1 : m_dispatchStack.back();
    node.focusId = focusId;
    node.keyContext = std::u16string(keyContext);
    m_building->dispatchNodes.push_back(std::move(node));
    m_dispatchStack.push_back(int(m_building->dispatchNodes.size() - 1));
}

void Window::popDispatchNode()
{
    if (m_dispatchStack.size() > 1)
        m_dispatchStack.pop_back();
}

void Window::onKeyEvent(KeyListener listener)
{
    if (m_building)
        m_building->keyListeners.push_back(KeyListenerEntry { m_dispatchStack.back(), listener });
}

void Window::onAction(std::u16string_view action, ActionListener listener)
{
    if (m_building)
        m_building->actionListeners.push_back(
                ActionListenerEntry { m_dispatchStack.back(), std::u16string(action), listener });
}

void Window::handleInput(const FocusHandle &focus, InputHandler *handler)
{
    if (m_building && focus && focus.id() == m_focused) {
        m_building->inputFocus = focus.id();
        m_building->inputHandler = handler;
    }
}

InputHandler *Window::inputHandler() const
{
    const Frame &f = rendered();
    return (f.inputHandler && f.inputFocus == m_focused && m_focused != 0) ? f.inputHandler : nullptr;
}

void Window::focus(const FocusHandle &handle)
{
    if (handle.id() == m_focused)
        return;
    m_focused = handle.id();
    m_pendingKeys.clear();
    refresh();
    m_platform.inputStateChanged();
}

void Window::blur()
{
    if (m_focused == 0)
        return;
    m_focused = 0;
    m_pendingKeys.clear();
    refresh();
    m_platform.inputStateChanged();
}

std::vector<int> Window::focusPath(const Frame &frame) const
{
    std::vector<int> path;
    int node = 0;
    if (m_focused != 0) {
        for (int i = int(frame.dispatchNodes.size()) - 1; i >= 0; --i) {
            if (frame.dispatchNodes[std::size_t(i)].focusId == m_focused) {
                node = i;
                break;
            }
        }
    }
    for (int n = node; n >= 0; n = frame.dispatchNodes.empty() ? -1 : frame.dispatchNodes[std::size_t(n)].parent)
        path.push_back(n);
    std::reverse(path.begin(), path.end());
    if (frame.dispatchNodes.empty())
        path.clear();
    return path;
}

bool Window::focusWithin(std::uint64_t focusId) const
{
    if (focusId == 0 || m_focused == 0)
        return false;
    const Frame &f = rendered();
    for (int n : focusPath(f)) {
        if (f.dispatchNodes[std::size_t(n)].focusId == focusId)
            return true;
    }
    return false;
}

bool Window::dispatchActionAlong(const std::vector<int> &path, std::size_t fromDepth, std::u16string_view action)
{
    const Frame &f = rendered();
    for (std::size_t d = fromDepth + 1; d-- > 0;) {
        const int node = path[d];
        for (auto it = f.actionListeners.rbegin(); it != f.actionListeners.rend(); ++it) {
            if (it->node == node && it->action == action) {
                it->listener(*this, m_app);
                return true;
            }
        }
    }
    return false;
}

bool Window::dispatchAction(std::u16string_view action)
{
    return m_app.update([&] {
        const std::vector<int> path = focusPath(rendered());
        return !path.empty() && dispatchActionAlong(path, path.size() - 1, action);
    });
}

bool Window::dispatchKey(const PlatformInput &input)
{
    if (const auto *m = std::get_if<ModifiersChangedEvent>(&input)) {
        m_modifiers = m->modifiers;
        return false;
    }
    const Frame &f = rendered();
    const std::vector<int> path = focusPath(f);
    if (path.empty())
        return false;
    m_propagate = true;
    for (std::size_t d = 0; d < path.size() && m_propagate; ++d) {
        for (const KeyListenerEntry &l : f.keyListeners) {
            if (l.node == path[d]) {
                l.listener(input, DispatchPhase::Capture, *this, m_app);
                if (!m_propagate)
                    break;
            }
        }
    }
    for (std::size_t d = path.size(); d-- > 0 && m_propagate;) {
        for (auto it = f.keyListeners.rbegin(); it != f.keyListeners.rend(); ++it) {
            if (it->node == path[d]) {
                it->listener(input, DispatchPhase::Bubble, *this, m_app);
                if (!m_propagate)
                    break;
            }
        }
    }
    if (!m_propagate)
        return true;
    const auto *down = std::get_if<KeyDownEvent>(&input);
    if (!down)
        return false;
    m_modifiers = down->keystroke.modifiers;

    // Key bindings: the deepest key context first, the global bindings last.
    auto tryBindings = [&](const std::vector<Keystroke> &typed, bool &pending) -> bool {
        const Keymap &keymap = m_app.keymap();
        for (std::size_t d = path.size(); d-- > 0;) {
            const DispatchNode &node = f.dispatchNodes[std::size_t(path[d])];
            if (node.keyContext.empty())
                continue;
            const Keymap::Result r = keymap.match(typed, node.keyContext);
            if (r.match == Keymap::Match::Complete) {
                m_pendingKeys.clear();
                if (dispatchActionAlong(path, d, r.binding->action))
                    return true;
            } else if (r.match == Keymap::Match::Pending) {
                pending = true;
            }
        }
        const Keymap::Result r = keymap.match(typed, {});
        if (r.match == Keymap::Match::Complete) {
            m_pendingKeys.clear();
            if (dispatchActionAlong(path, path.size() - 1, r.binding->action))
                return true;
        } else if (r.match == Keymap::Match::Pending) {
            pending = true;
        }
        return false;
    };
    m_pendingKeys.push_back(down->keystroke);
    bool pending = false;
    if (tryBindings(m_pendingKeys, pending))
        return true;
    if (!pending && m_pendingKeys.size() > 1) {
        // The sequence broke off: the last keystroke on its own.
        m_pendingKeys.assign(1, down->keystroke);
        if (tryBindings(m_pendingKeys, pending))
            return true;
    }
    if (pending)
        return true;
    m_pendingKeys.clear();

    // Text for the focused element's input handler.
    const Modifiers &mods = down->keystroke.modifiers;
    if (!down->keystroke.keyChar.empty() && !mods.control && !mods.platform) {
        if (InputHandler *handler = inputHandler()) {
            handler->replaceText(std::nullopt, down->keystroke.keyChar);
            return true;
        }
    }
    return false;
}

bool Window::dispatchMouse(const PlatformInput &input)
{
    Frame &f = rendered();
    if (std::holds_alternative<MouseExitEvent>(input))
        m_mouse = Point { -1e9f, -1e9f };
    else
        m_mouse = positionOf(input);
    std::visit(
            [&](const auto &e) {
                if constexpr (requires { e.modifiers; })
                    m_modifiers = e.modifiers;
            },
            input);
    const std::vector<HitboxId> before = f.hitTest;
    computeHitTest(f, m_mouse);
    if (before != f.hitTest)
        refresh(); // hover styles
    m_propagate = true;
    for (std::size_t i = 0; i < f.mouseListeners.size() && m_propagate; ++i)
        f.mouseListeners[i](input, DispatchPhase::Capture, *this, m_app);
    for (std::size_t i = f.mouseListeners.size(); i-- > 0 && m_propagate;)
        f.mouseListeners[i](input, DispatchPhase::Bubble, *this, m_app);
    updateCursor();
    return !f.hitTest.empty() || !m_propagate;
}

bool Window::dispatchInput(const PlatformInput &input)
{
    m_defaultPrevented = false;
    return m_app.update([&] { return isMouseEvent(input) ? dispatchMouse(input) : dispatchKey(input); });
}

// --- painting

void Window::paintQuad(Rect bounds, const std::optional<Background> &background, Edges borderWidths, Color borderColor,
        Corners cornerRadii)
{
    if (!m_building)
        return;
    const bool hasBackground = background && !background->isTransparent();
    const bool hasBorder = !borderWidths.isZero() && !borderColor.isTransparent();
    if (!hasBackground && !hasBorder)
        return;
    const Rect device = bounds.scaled(m_scale).snapped();
    if (device.isEmpty())
        return;
    Quad q;
    q.bounds = device;
    q.mask = deviceMask();
    if (!q.bounds.intersects(q.mask.bounds))
        return;
    if (hasBackground) {
        q.background = *background;
        q.background.color = faded(q.background.color);
        q.background.color2 = faded(q.background.color2);
    } else {
        q.background = Background::solid(transparent());
    }
    q.borderColor = hasBorder ? faded(borderColor) : transparent();
    q.borderWidths = hasBorder ? borderWidths * m_scale : Edges {};
    q.cornerRadii = (cornerRadii * m_scale).clampedTo(device.size);
    m_building->scene.insert(q);
}

void Window::paintShadows(Rect bounds, Corners cornerRadii, std::span<const BoxShadow> shadows)
{
    if (!m_building)
        return;
    for (const BoxShadow &s : shadows) {
        if (s.color.isTransparent())
            continue;
        Rect r = bounds.translated(s.offset);
        r = s.spreadRadius >= 0 ? r.dilated(s.spreadRadius) : r.inset(Edges::all(-s.spreadRadius));
        Corners radii = cornerRadii;
        radii.topLeft = std::max(0.0f, radii.topLeft + s.spreadRadius);
        radii.topRight = std::max(0.0f, radii.topRight + s.spreadRadius);
        radii.bottomRight = std::max(0.0f, radii.bottomRight + s.spreadRadius);
        radii.bottomLeft = std::max(0.0f, radii.bottomLeft + s.spreadRadius);
        Shadow shadow;
        shadow.bounds = r.scaled(m_scale);
        shadow.mask = deviceMask();
        shadow.cornerRadii = (radii * m_scale).clampedTo(shadow.bounds.size);
        shadow.color = faded(s.color);
        shadow.blurRadius = s.blurRadius * m_scale;
        if (primitiveBounds(Primitive(shadow)).intersects(shadow.mask.bounds))
            m_building->scene.insert(shadow);
    }
}

void Window::paintGlyph(FaceId face, std::uint32_t glyph, float fontSize, Point baselineOrigin, Color color)
{
    if (!m_building || fontSize <= 0)
        return;
    const Point device = baselineOrigin * m_scale;
    const float ix = std::floor(device.x);
    const int subpixel = std::clamp(int((device.x - ix) * 4), 0, 3);
    const float iy = std::round(device.y);
    const std::optional<GlyphImage> image
            = m_app.textSystem().glyph(m_app.atlas(), face, glyph, fontSize * m_scale, subpixel);
    if (!image || image->tile.isEmpty())
        return;
    const Rect bounds = Rect::fromXYWH(ix + float(image->left), iy + float(image->top), float(image->tile.width),
            float(image->tile.height));
    const ContentMask mask = deviceMask();
    if (!bounds.intersects(mask.bounds))
        return;
    if (image->color) {
        ColorSprite s;
        s.bounds = bounds;
        s.mask = mask;
        s.tile = image->tile;
        s.opacity = opacity() * color.a;
        m_building->scene.insert(s);
    } else {
        MonochromeSprite s;
        s.bounds = bounds;
        s.mask = mask;
        s.color = faded(color);
        s.tile = image->tile;
        m_building->scene.insert(s);
    }
}

void Window::paintText(const TextLayout &layout, Point origin, std::span<const TextPaintStyle> styles, TextAlign align,
        float boxWidth)
{
    if (!m_building)
        return;
    static const TextPaintStyle kDefault;
    auto styleOf = [&](std::uint32_t index) -> const TextPaintStyle & {
        if (styles.empty())
            return kDefault;
        return styles[std::min<std::size_t>(index, styles.size() - 1)];
    };
    const Rect mask = contentMask();
    const auto &glyphs = layout.glyphs();
    const FontCollection &fonts = m_app.textSystem().fonts();
    for (const TextLine &line : layout.lines()) {
        float dx = 0;
        if (align == TextAlign::Center)
            dx = std::max(0.0f, (boxWidth - line.width) / 2);
        else if (align == TextAlign::Right)
            dx = std::max(0.0f, boxWidth - line.width);
        const float top = origin.y + line.top;
        if (top > mask.bottom() || top + line.height < mask.top())
            continue; // off screen: a long paragraph in a scrolled list
        const std::uint32_t end = line.firstGlyph + line.glyphCount;

        // Runs of one style on this line: backgrounds first, then glyphs, then decorations.
        struct Segment {
            std::uint32_t style;
            float x0;
            float x1;
            FaceId face;
            float fontSize;
        };
        std::vector<Segment> segments;
        for (std::uint32_t i = line.firstGlyph; i < end; ++i) {
            const PositionedGlyph &g = glyphs[i];
            const float x = g.position.x;
            if (segments.empty() || segments.back().style != g.styleIndex) {
                if (!segments.empty())
                    segments.back().x1 = x;
                segments.push_back(Segment { g.styleIndex, x, x, g.face, g.fontSize });
            }
        }
        if (!segments.empty())
            segments.back().x1 = std::max(segments.back().x0, layout.caretX(line.end, CaretAffinity::Upstream));
        for (const Segment &s : segments) {
            const TextPaintStyle &style = styleOf(s.style);
            if (style.background && !style.background->isTransparent())
                paintQuad(Rect::fromLTRB(origin.x + dx + s.x0, top, origin.x + dx + s.x1, top + line.height),
                        Background::solid(*style.background), {}, transparent(), {});
        }
        for (std::uint32_t i = line.firstGlyph; i < end; ++i) {
            const PositionedGlyph &g = glyphs[i];
            paintGlyph(g.face, g.glyph, g.fontSize, { origin.x + dx + g.position.x, origin.y + g.position.y },
                    styleOf(g.styleIndex).color);
        }
        for (const Segment &s : segments) {
            const TextPaintStyle &style = styleOf(s.style);
            if (!style.underline && !style.strikethrough)
                continue;
            const FontMetrics metrics = fonts.metrics(s.face);
            const float baseline = origin.y + line.baseline;
            if (style.underline) {
                const UnderlineStyle &u = *style.underline;
                const float thickness = std::max(u.thickness, metrics.underlineThickness * s.fontSize);
                const Color color = u.color.isTransparent() ? style.color : u.color;
                paintUnderline({ origin.x + dx + s.x0, baseline + std::max(1.0f, metrics.underlinePosition * s.fontSize) },
                        s.x1 - s.x0, thickness, color, u.wavy);
            }
            if (style.strikethrough) {
                const float thickness = std::max(1.0f, metrics.strikeoutThickness * s.fontSize);
                const float y = baseline - (metrics.strikeoutPosition > 0 ? metrics.strikeoutPosition : 0.3f) * s.fontSize;
                paintQuad(Rect::fromXYWH(origin.x + dx + s.x0, y - thickness / 2, s.x1 - s.x0, thickness),
                        Background::solid(style.color), {}, transparent(), {});
            }
        }
    }
}

void Window::paintPicture(Rect bounds, Corners cornerRadii, std::shared_ptr<const ImageData> image, float opacity_,
        bool grayscale)
{
    if (!m_building || !image || image->width <= 0 || image->height <= 0)
        return;
    Picture p;
    p.bounds = bounds.scaled(m_scale);
    p.mask = deviceMask();
    if (!p.bounds.intersects(p.mask.bounds))
        return;
    p.cornerRadii = (cornerRadii * m_scale).clampedTo(p.bounds.size);
    p.image = std::move(image);
    p.opacity = opacity_ * opacity();
    p.grayscale = grayscale;
    m_building->scene.insert(std::move(p));
}

void Window::insertPath(PathBuilder &&builder, Color color)
{
    if (!m_building)
        return;
    PathPrimitive path = std::move(builder).build(faded(color), deviceMask());
    if (path.points.size() >= 3 && path.bounds.intersects(path.mask.bounds))
        m_building->scene.insert(std::move(path));
}

void Window::paintUnderline(Point origin, float width, float thickness, Color color, bool wavy)
{
    if (!m_building || width <= 0)
        return;
    Underline u;
    const float height = wavy ? thickness * 3 : thickness;
    u.bounds = Rect::fromXYWH(origin.x, origin.y, width, height).scaled(m_scale);
    u.mask = deviceMask();
    u.color = faded(color);
    u.thickness = thickness * m_scale;
    u.wavy = wavy;
    if (u.bounds.intersects(u.mask.bounds))
        m_building->scene.insert(u);
}

} // namespace brulion::atrament
