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
//
// A finger's or a pen's press, drag and release go through the window's touch gesture first
// (docs/ATRAMENT.md "Touch", ui/touch.cpp): a drag past the slop scrolls the innermost container
// under it that can still move that way, its release flings it; the elements see a tap, a press
// held still, or a drag nothing scrolls.

#include "base/arena.h"
#include "base/function.h"
#include "layout/layout.h"
#include "scene/scene.h"
#include "text/textlayout.h"
#include "ui/accessibility.h"
#include "ui/app.h"
#include "ui/element.h"
#include "ui/events.h"
#include "ui/platform.h"
#include "ui/style.h"
#include "ui/touch.h"

#include <cstdint>
#include <functional>
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
    PlatformWindow *platformWindow() { return m_platform; }
    // Leaves the platform window (it is going away); the window draws and dispatches no more.
    void detachPlatform();
    Size viewportSize() const { return m_viewport; }
    float scaleFactor() const { return m_scale; }
    Point mousePosition() const { return m_mouse; }
    Modifiers modifiers() const { return m_modifiers; }
    bool isActive() const { return m_platform && m_platform->isActive(); }

    // Asks for a frame (coalesced).
    void refresh();
    // Draws a frame now (the platform's frame callback calls it).
    void draw();
    // The scene of the frame on screen.
    const Scene &scene() const;
    std::uint64_t frameCount() const { return m_frameCount; }

    // Input from the platform; returns whether a listener handled it.
    bool dispatchInput(const PlatformInput &input);
    // The last touch became a scroll (until the next one goes down): its release was no tap.
    bool touchScrolled() const { return m_touchScrolled; }
    // A finger (or a pen) is down, and whether its drag is scrolling now.
    bool touchActive() const { return m_touch.phase != TouchPhaseState::None; }
    bool touchScrolling() const { return m_touch.phase == TouchPhaseState::Scrolling; }
    bool flinging() const { return m_fling.active; }
    void stopFling();
    // What a finger did, for diagnosis (the Qt host logs it under brulion.atrament.touch).
    struct TouchTrace {
        enum class Kind : std::uint8_t { Scroll, Release, FlingEnd };
        Kind kind = Kind::Scroll;
        Point position;     // the finger's
        Point offset;       // the container's scroll offset
        Point velocity;     // Release: the finger's, px/s
        Point distance;     // Release: the fling's (zero: no fling)
        double seconds = 0; // Release: the fling's duration; FlingEnd: how long it ran
        int frames = 0;     // FlingEnd: the frames it moved in
    };
    void setTouchTrace(std::function<void(const TouchTrace &)> trace) { m_touchTrace = std::move(trace); }
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
    // The frame on screen was built with the focus where it is now (inputHandler() is the focused
    // element's); false between a change of focus and its frame.
    bool focusDrawn() const { return m_drawnFocus == m_focused; }
    // Dispatches an action by name along the focused path, as a key binding would.
    bool dispatchAction(std::u16string_view action);

    // --- accessibility (ui/accessibility.h)
    // The host turns it on while an assistive technology listens (tests turn it on themselves):
    // only then does a frame build its semantic tree.
    bool accessibilityEnabled() const { return m_accessibilityEnabled; }
    void setAccessibilityEnabled(bool on);
    // The tree of the frame on screen (empty while disabled).
    const AccessibilityTree &accessibilityTree() const { return rendered().accessibility; }
    // An assistive technology's action on a node of the frame on screen, dispatched like input to
    // the element's listeners; returns whether something handled it.
    bool performAccessibilityAction(AccessNodeId node, const AccessibilityActionRequest &request);
    bool performAccessibilityAction(AccessNodeId node, AccessibilityAction action)
    {
        return performAccessibilityAction(node, AccessibilityActionRequest{action, {}, {}, 0, {}});
    }

    // --- used by elements during paint (no-ops returning nullptr while disabled or hidden)
    // A node of the current element — its id is the element's path (with `key` mixed in, for
    // several nodes of one element) — and the parent of what is painted until
    // endAccessibilityNode(), which closes a node this returned.
    AccessibilityNode *beginAccessibilityNode(Role role, Rect bounds, std::uint64_t key = 0);
    void endAccessibilityNode();
    // The node the current element's content belongs to (the innermost begun).
    AccessibilityNode *currentAccessibilityNode();
    // A list's row waiting for its root element to describe it (claimed once), or nullptr.
    AccessibilityNode *adoptAccessibilityNode();
    // Text of the current element: into the name of a node named by its content (a button), else
    // a text node of its own.
    void addAccessibilityText(std::u16string_view text, Rect bounds, const TextLayout *layout = nullptr);
    // Nothing painted until the pop is in the tree (decoration).
    void pushAccessibilityHidden();
    void popAccessibilityHidden();
    // Copies into the frame arena (names and values that may change before the next frame).
    std::u16string_view accessibilityString(std::u16string_view s);

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

    // The scroll containers around what is prepainted now (a scrolling Div or list pushes itself
    // around its children; `viewport` in window coordinates). With its hitbox it is also one a
    // finger's drag scrolls.
    void pushScrollFrame(ScrollState &state, Rect viewport, bool x, bool y, HitboxId hitbox = 0);
    void popScrollFrame();
    // During prepaint: after this frame the scroll containers around the current element scroll
    // the least that shows `rect` (window coordinates; the last request of a frame wins) — a
    // focused field's caret when the window gets shorter (an on-screen keyboard) or the caret
    // moves out of view.
    void revealRect(Rect rect);

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
    // The dispatch nodes painted so far, and whether the focused element is among those painted
    // since such a mark (a list finds the row with the focus).
    std::size_t dispatchNodeMark() const { return m_building ? m_building->dispatchNodes.size() : 0; }
    bool focusPaintedSince(std::size_t mark) const;
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
        double buildMicros = 0; // render, layout, prepaint and paint
        double presentMicros = 0; // the platform's present (the CPU renderer's time, hosted)
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
    struct ScrollTarget { // a container a finger can scroll
        ScrollState *state = nullptr;
        HitboxId hitbox = 0;
        bool x = false;
        bool y = false;
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
        std::vector<ScrollTarget> scrollTargets; // in prepaint order: outer containers first
        std::uint64_t inputFocus = 0;
        InputHandler *inputHandler = nullptr;
        std::vector<EntityId> views; // sorted after the frame
        AccessibilityTree accessibility;
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
    std::vector<HitboxId> hitsAt(const Frame &frame, Point position) const;
    void updateCursor();
    bool dispatchMouse(const PlatformInput &input);
    // --- touch (ui/touch.cpp)
    bool isTouchInput(const PlatformInput &input) const;
    bool dispatchTouch(const PlatformInput &input);
    bool touchDown(const MouseDownEvent &down);
    bool touchMove(const MouseMoveEvent &move);
    bool touchUp(const MouseUpEvent &up);
    void deliverTouchPress();
    void touchLongPressed();
    bool beginTouchScroll(Point dragged);
    void cancelTouchPress();
    void touchHoverOff();
    void endTouch();
    double touchSeconds(EventTime time) const;
    // The containers under a point that a finger could scroll, topmost (innermost) first.
    std::vector<ScrollTarget> scrollTargetsAt(Point position) const;
    ScrollState *liveScrollTarget(ScrollState *state) const;
    void startFling(ScrollState *state, bool x, bool y, Point velocity);
    void stepFling();
    bool dispatchKey(const PlatformInput &input);
    std::vector<int> focusPath(const Frame &frame) const;
    bool dispatchActionAlong(const std::vector<int> &path, std::size_t fromDepth, std::u16string_view action);
    void insertPath(PathBuilder &&builder, Color color);
    bool scrollIntoView(const AccessibilityTree &tree, const AccessibilityNode &node);
    bool scrollPage(const AccessibilityNode &node, bool forward);
    ContentMask deviceMask() const;
    Color faded(Color c) const { return c.fadedBy(opacity()); }
    Frame &rendered() { return m_frames[m_renderedIndex]; }
    const Frame &rendered() const { return m_frames[m_renderedIndex]; }

    App &m_app;
    PlatformWindow *m_platform;
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
    struct ScrollFrame {
        ScrollState *state = nullptr;
        Rect viewport;
        bool x = false;
        bool y = false;
    };
    std::vector<ScrollFrame> m_scrollFrames;
    std::vector<ScrollFrame> m_revealFrames; // revealRect's containers, innermost last
    enum class TouchPhaseState : std::uint8_t {
        None,
        Waiting,   // down inside a container that can scroll: the elements do not have the press yet
        Pressed,   // the elements have the press; a drag past the slop may still scroll
        Scrolling, // the drag scrolls `target`; the elements' press was cancelled (or never given)
        Held,      // a long press in the focused field: it selects, the finger scrolls nothing
    };
    struct TouchGesture {
        TouchPhaseState phase = TouchPhaseState::None;
        MouseDownEvent down; // the press, kept for the elements while Waiting
        Point last;
        bool decided = false; // the drag passed the slop: it scrolls, or never will
        bool scrollOnly = false; // MouseDownEvent::scrollOnly: no element ever gets this touch
        ScrollState *target = nullptr;
        bool x = false;
        bool y = false;
        VelocityTracker velocity;
        Subscription pressTimer;
        Subscription longPressTimer;
    };
    TouchGesture m_touch;
    bool m_touchScrolled = false;
    struct Fling {
        bool active = false;
        ScrollState *target = nullptr;
        FlingCurve x;
        FlingCurve y;
        std::chrono::steady_clock::time_point start;
        Point travelled; // what the curves moved so far (the container may have moved otherwise too)
        int frames = 0;
    };
    Fling m_fling;
    std::function<void(const TouchTrace &)> m_touchTrace;
    std::optional<Rect> m_reveal;
    bool revealPending();
    std::unordered_map<std::uint64_t, std::unique_ptr<AnyState>> m_states;
    std::uint64_t m_focused = 0;
    std::uint64_t m_drawnFocus = 0; // m_focused when the frame on screen was built
    std::vector<Keystroke> m_pendingKeys;
    std::uint64_t m_frameCount = 0;
    bool m_propagate = true;
    bool m_defaultPrevented = false;
    bool m_refreshRequested = false;
    bool m_drawing = false;
    CursorStyle m_cursor = CursorStyle::Arrow;
    FrameStats m_stats;
    bool m_accessibilityEnabled = false;
    std::vector<AccessibilityNode *> m_accessStack;
    int m_accessHidden = 0;
    std::vector<AccessibilityEvent> m_accessEvents;
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
