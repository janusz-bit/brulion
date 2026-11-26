#pragma once

// A window (docs/ATRAMENT.md "A frame"): draws its root view into a scene when asked, and
// dispatches input to the listeners the last frame registered.
//
// A frame: render the root view → request layout → compute layout → prepaint (bounds, hitboxes,
// element state) → hit test → paint (primitives, listeners) → present. Frames double-buffer:
// input between two frames goes to the listeners of the frame on screen, which live in its arena
// until the next frame is complete.
//
// Elements call the methods below during their phases; coordinates are the window's logical
// pixels, and the scene receives device pixels (× scaleFactor()).

#include "base/arena.h"
#include "base/function.h"
#include "layout/layout.h"
#include "scene/scene.h"
#include "text/textlayout.h"
#include "ui/app.h"
#include "ui/element.h"
#include "ui/events.h"
#include "ui/platform.h"
#include "ui/style.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace brulion::atrament {

using HitboxId = std::uint32_t; // 0: none

enum class HitboxBehavior : std::uint8_t {
    Normal, // hit, and what is below too
    BlockMouse, // hit; nothing below it is (an opaque panel, a popup)
};

// How a run of text is painted (TextLayout's glyphs carry an index into a span of these).
struct TextPaintStyle {
    Color color = black();
    std::optional<UnderlineStyle> underline;
    bool strikethrough = false;
    std::optional<Color> background;
};

class Window {
public:
    Window(App &app, PlatformWindow &platform, AnyView root);
    ~Window();
    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;

    App &app() { return m_app; }
    PlatformWindow &platformWindow() { return m_platform; }
    Size viewportSize() const { return m_viewport; }
    float scaleFactor() const { return m_scale; }
    Point mousePosition() const { return m_mouse; }
    Modifiers modifiers() const { return m_modifiers; }
    bool isActive() const { return m_platform.isActive(); }

    // Asks for a frame (coalesced).
    void refresh();
    // Draws a frame now (the platform's frame callback calls it).
    void draw();
    // The scene of the frame on screen.
    const Scene &scene() const;
    std::uint64_t frameCount() const { return m_frameCount; }

    // Input from the platform; returns whether a listener handled it.
    bool dispatchInput(const PlatformInput &input);
    // Stops the event at the current listener (no further bubbling, no default action).
    void stopPropagation() { m_propagate = false; }
    bool propagating() const { return m_propagate; }
    // Marks the event's default action taken (e.g. focus moved by an inner element on a mouse
    // down): outer elements check it and leave it alone. Reset for every event.
    void preventDefault() { m_defaultPrevented = true; }
    bool defaultPrevented() const { return m_defaultPrevented; }

    // --- focus
    std::uint64_t focusedId() const { return m_focused; }
    void focus(const FocusHandle &handle);
    void blur();
    bool isFocused(std::uint64_t focusId) const { return focusId != 0 && m_focused == focusId; }
    // The focus is on `focusId`'s element or inside it (on the frame on screen).
    bool focusWithin(std::uint64_t focusId) const;
    // The input handler of the focused element, or nullptr (the platform's IME asks).
    InputHandler *inputHandler() const;
    // Dispatches an action by name along the focused path, as a key binding would.
    bool dispatchAction(std::u16string_view action);

    // --- views drawn by the frame (for notifications)
    void recordView(EntityId id);
    bool drewEntity(EntityId id) const;

    // --- used by elements during requestLayout
    NodeId requestLayout(const LayoutStyle &style, std::span<const NodeId> children);
    NodeId requestMeasuredLayout(const LayoutStyle &style, MeasureFunction measure);
    LayoutEngine &layoutEngine() { return m_layout; }

    // --- identity and state (all phases)
    // The id of the element being processed (its path from the root).
    std::uint64_t currentElementId() const { return m_idStack.back(); }
    void pushElementId(ElementId id);
    void popElementId();
    // State of type S kept for the current element across frames (default-constructed the first
    // time); dropped after a frame that did not ask for it.
    template<class S>
    S &elementState();

    // --- used by elements during prepaint and paint
    // The bounds of a layout node (window coordinates, with the current element offset).
    Rect layoutBounds(NodeId node) const;
    NodeLayout nodeLayout(NodeId node) const { return m_layout.layout(node); }
    Point elementOffset() const { return m_offsets.back(); }
    void pushElementOffset(Point offset); // added to the current one
    void popElementOffset();
    // The visible part (window logical px); painting is cut to it.
    Rect contentMask() const { return m_masks.back(); }
    void pushContentMask(Rect mask); // intersected with the current one
    void popContentMask();
    float opacity() const { return m_opacity.back(); }
    void pushOpacity(float opacity); // multiplied
    void popOpacity();
    const TextStyle &textStyle() const { return m_textStyles.back(); }
    void pushTextStyle(const TextStyleRefinement &refinement);
    void popTextStyle();

    HitboxId insertHitbox(Rect bounds, HitboxBehavior behavior = HitboxBehavior::Normal);
    // Under the pointer in the frame being built (after prepaint) or on screen (between frames).
    bool isHovered(HitboxId hitbox) const;
    Rect hitboxBounds(HitboxId hitbox) const;
    // The cursor while the pointer is over the hitbox (the topmost request wins).
    void setCursorStyle(HitboxId hitbox, CursorStyle cursor);

    // Mouse listeners run for every mouse event, capture then bubble; they test hover themselves.
    using MouseListener = Fn<void(const PlatformInput &, DispatchPhase, Window &, App &)>;
    void onMouseEvent(MouseListener listener);

    // Key dispatch: a node of the dispatch tree for the current element (paint phase), with its
    // focus handle and key context; key and action listeners attach to the current node.
    void pushDispatchNode(std::uint64_t focusId, std::u16string_view keyContext);
    void popDispatchNode();
    using KeyListener = Fn<void(const PlatformInput &, DispatchPhase, Window &, App &)>;
    void onKeyEvent(KeyListener listener);
    using ActionListener = Fn<void(Window &, App &)>;
    void onAction(std::u16string_view action, ActionListener listener);
    // The focused element takes text through `handler` (an arena object of this frame).
    void handleInput(const FocusHandle &focus, InputHandler *handler);

    // --- painting (logical px in, device px into the scene)
    void paintQuad(Rect bounds, const std::optional<Background> &background, Edges borderWidths, Color borderColor,
            Corners cornerRadii);
    void paintShadows(Rect bounds, Corners cornerRadii, std::span<const BoxShadow> shadows);
    // A paragraph at `origin` (its top-left), each glyph styled by its styleIndex; lines aligned
    // within `boxWidth` (centre / right).
    void paintText(const TextLayout &layout, Point origin, std::span<const TextPaintStyle> styles,
            TextAlign align = TextAlign::Left, float boxWidth = 0);
    void paintGlyph(FaceId face, std::uint32_t glyph, float fontSize, Point baselineOrigin, Color color);
    void paintPicture(Rect bounds, Corners cornerRadii, std::shared_ptr<const ImageData> image, float opacity = 1,
            bool grayscale = false);
    // `build` draws in logical px into a builder whose points are scaled to device px.
    class PathPainter {
    public:
        void moveTo(Point p) { m_builder.moveTo(p * m_scale); }
        void lineTo(Point p) { m_builder.lineTo(p * m_scale); }
        void quadTo(Point c, Point p) { m_builder.quadTo(c * m_scale, p * m_scale); }
        void cubicTo(Point c1, Point c2, Point p) { m_builder.cubicTo(c1 * m_scale, c2 * m_scale, p * m_scale); }
        void arcTo(Point center, float radius, float a0, float a1)
        {
            m_builder.arcTo(center * m_scale, radius * m_scale, a0, a1);
        }
        void close() { m_builder.close(); }

    private:
        friend class Window;
        explicit PathPainter(float scale)
            : m_scale(scale)
        {
        }
        PathBuilder m_builder;
        float m_scale;
    };
    template<class F>
    void paintPath(Color color, F &&build)
    {
        PathPainter painter(m_scale);
        build(painter);
        insertPath(std::move(painter.m_builder), color);
    }
    void paintUnderline(Point origin, float width, float thickness, Color color, bool wavy);

    // Scene statistics of the last frame, for tests.
    struct FrameStats {
        std::size_t primitives = 0;
        std::size_t layoutNodes = 0;
        std::size_t hitboxes = 0;
        std::size_t arenaBytes = 0;
    };
    FrameStats lastFrameStats() const { return m_stats; }

private:
    struct Hitbox {
        Rect bounds; // clipped by the content mask
        HitboxBehavior behavior = HitboxBehavior::Normal;
    };
    struct CursorRequest {
        HitboxId hitbox;
        CursorStyle cursor;
    };
    struct DispatchNode {
        int parent = -1;
        std::uint64_t focusId = 0;
        std::u16string keyContext;
    };
    struct KeyListenerEntry {
        int node;
        KeyListener listener;
    };
    struct ActionListenerEntry {
        int node;
        std::u16string action;
        ActionListener listener;
    };
    struct Frame {
        Arena arena { 256 * 1024 };
        Scene scene;
        std::vector<Hitbox> hitboxes;
        std::vector<HitboxId> hitTest; // topmost first
        std::vector<CursorRequest> cursors;
        std::vector<MouseListener> mouseListeners;
        std::vector<DispatchNode> dispatchNodes;
        std::vector<KeyListenerEntry> keyListeners;
        std::vector<ActionListenerEntry> actionListeners;
        std::uint64_t inputFocus = 0;
        InputHandler *inputHandler = nullptr;
        std::vector<EntityId> views; // sorted after the frame
        void clear();
    };
    struct AnyState {
        virtual ~AnyState() = default;
        std::type_index type = typeid(void);
        std::uint64_t lastFrame = 0;
    };
    template<class S>
    struct StateBox final : AnyState {
        S value {};
    };

    void computeHitTest(Frame &frame, Point position);
    void updateCursor();
    bool dispatchMouse(const PlatformInput &input);
    bool dispatchKey(const PlatformInput &input);
    std::vector<int> focusPath(const Frame &frame) const;
    bool dispatchActionAlong(const std::vector<int> &path, std::size_t fromDepth, std::u16string_view action);
    void insertPath(PathBuilder &&builder, Color color);
    ContentMask deviceMask() const;
    Color faded(Color c) const { return c.fadedBy(opacity()); }
    Frame &rendered() { return m_frames[m_renderedIndex]; }
    const Frame &rendered() const { return m_frames[m_renderedIndex]; }

    App &m_app;
    PlatformWindow &m_platform;
    AnyView m_root;
    Frame m_frames[2];
    int m_renderedIndex = 0;
    Frame *m_building = nullptr; // during draw()
    LayoutEngine m_layout;
    Size m_viewport;
    float m_scale = 1;
    Point m_mouse { -1e9f, -1e9f };
    Modifiers m_modifiers;
    std::vector<std::uint64_t> m_idStack;
    std::vector<Point> m_offsets;
    std::vector<Rect> m_masks;
    std::vector<float> m_opacity;
    std::vector<TextStyle> m_textStyles;
    std::vector<int> m_dispatchStack;
    std::unordered_map<std::uint64_t, std::unique_ptr<AnyState>> m_states;
    std::uint64_t m_focused = 0;
    std::vector<Keystroke> m_pendingKeys;
    std::uint64_t m_frameCount = 0;
    bool m_propagate = true;
    bool m_defaultPrevented = false;
    bool m_refreshRequested = false;
    bool m_drawing = false;
    CursorStyle m_cursor = CursorStyle::Arrow;
    FrameStats m_stats;
};

template<class S>
S &Window::elementState()
{
    const std::uint64_t key = hashCombine(currentElementId(), std::hash<std::type_index> {}(typeid(S)));
    auto &slot = m_states[key];
    if (!slot || slot->type != std::type_index(typeid(S))) {
        auto box = std::make_unique<StateBox<S>>();
        box->type = typeid(S);
        slot = std::move(box);
    }
    slot->lastFrame = m_frameCount;
    return static_cast<StateBox<S> *>(slot.get())->value;
}

// RAII helpers for the stacks above.
struct ElementIdScope {
    Window &window;
    ElementIdScope(Window &w, ElementId id)
        : window(w)
    {
        w.pushElementId(id);
    }
    ~ElementIdScope() { window.popElementId(); }
};

} // namespace brulion::atrament
