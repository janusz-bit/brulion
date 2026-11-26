#pragma once

// The platform interface (docs/ATRAMENT.md "Architecture"): what Atrament needs from the system —
// windows that take a frame and give input, a main-thread queue, timers, the clipboard, cursors.
// Implemented by the Qt host (src/atrament/qt, today), by the test platform (ui/testplatform.h) and
// later by Wayland, Win32, Android and the browser directly.

#include "base/geometry.h"
#include "ui/events.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace brulion::atrament {

class Scene;
class Atlas;
struct AccessibilityEvent;

enum class CursorStyle : std::uint8_t {
    Arrow,
    IBeam,
    PointingHand,
    Crosshair,
    OpenHand,
    ClosedHand,
    ResizeLeftRight,
    ResizeUpDown,
    ResizeColumn,
    ResizeRow,
    NotAllowed,
    Wait,
    None,
};

// The text editing interface of a focused element, for the platform's input method (D10): how the
// candidate window, autocorrect, gesture typing and composition (preedit) reach an editor. Offsets
// are UTF-16 code units of the element's text. Modelled on NSTextInputClient and Android's
// InputConnection; the Qt host maps it onto inputMethodQuery / QInputMethodEvent.
class InputHandler {
public:
    virtual ~InputHandler() = default;
    virtual std::u16string textForRange(Range range) = 0;
    virtual int textLength() = 0;
    // The selection (anchor may be after focus); nullopt when the element has none.
    struct Selection {
        int anchor = 0;
        int focus = 0;
        Range range() const { return { std::min(anchor, focus), std::max(anchor, focus) }; }
        bool operator==(const Selection &) const = default;
    };
    virtual std::optional<Selection> selection() = 0;
    // The text being composed (shown underlined), if any.
    virtual std::optional<Range> markedRange() = 0;
    // Replaces `range` (nullopt: the marked text, else the selection) with committed text.
    virtual void replaceText(std::optional<Range> range, std::u16string_view text) = 0;
    // Replaces `range` (as above) with composing text, marked; `selectedWithinMarked` places the
    // caret / selection inside it (nullopt: at its end).
    virtual void replaceAndMarkText(std::optional<Range> range, std::u16string_view text,
            std::optional<Range> selectedWithinMarked)
            = 0;
    virtual void unmarkText() = 0;
    virtual void setSelection(Selection selection) = 0;
    // Where a range is on screen (window logical px), for the candidate window.
    virtual std::optional<Rect> boundsForRange(Range range) = 0;
    // The offset under a point (window logical px), or nullopt when it is outside the text.
    virtual std::optional<int> offsetForPoint(Point point) = 0;
    // The element's own rectangle (window logical px): where a touch edits it, where a virtual
    // keyboard must not cover it.
    virtual std::optional<Rect> inputBounds() { return std::nullopt; }
    // Hints for a virtual keyboard.
    enum class Purpose : std::uint8_t { Text, Url, Email, Number, Password, Multiline };
    virtual Purpose purpose() { return Purpose::Multiline; }
};

class PlatformWindow {
public:
    virtual ~PlatformWindow() = default;

    virtual Size contentSize() const = 0; // logical px
    virtual float scaleFactor() const = 0; // device px per logical px
    virtual Point mousePosition() const = 0;
    virtual Modifiers modifiers() const = 0;
    virtual bool isActive() const = 0;

    // Asks for one frame: the platform calls `Callbacks::frame` once, at its next vertical blank.
    virtual void requestFrame() = 0;
    // Shows a frame (called from inside `Callbacks::frame`). A GPU renderer mirrors the atlas's
    // pages as textures and marks them clean once it has copied what changed.
    virtual void present(const Scene &scene, Atlas &atlas) = 0;
    virtual void setCursor(CursorStyle cursor) = 0;
    virtual void setTitle(std::u16string_view) { }
    // The focused element's text input state changed (its selection, its marked text, whether it
    // takes text at all): the input method should ask again.
    virtual void inputStateChanged() { }
    // The frame just presented changed the window's accessibility tree (ui/accessibility.h; only
    // while Window::accessibilityEnabled()). The events' strings are valid during the call.
    virtual void accessibilityChanged(const std::vector<AccessibilityEvent> &) { }

    struct Callbacks {
        std::function<void()> frame;
        // Returns whether the input was handled.
        std::function<bool(const PlatformInput &)> input;
        std::function<void()> resized;
        std::function<void(bool)> activeChanged;
        std::function<void()> closed;
        // The focused element's input handler, or nullptr when no element takes text.
        std::function<InputHandler *()> inputHandler;
    };
    void setCallbacks(Callbacks callbacks) { m_callbacks = std::move(callbacks); }
    const Callbacks &callbacks() const { return m_callbacks; }

protected:
    Callbacks m_callbacks;
};

struct WindowOptions {
    std::u16string title;
    Size size { 1024, 768 };
};

using TimerId = std::uint64_t;

class Platform {
public:
    virtual ~Platform() = default;

    // A window of the platform's own; hosts that make their windows themselves return nullptr.
    virtual std::unique_ptr<PlatformWindow> openWindow(const WindowOptions &options) = 0;

    // Runs `task` on the main thread soon (never inside the call).
    virtual void post(std::function<void()> task) = 0;
    virtual TimerId postDelayed(std::chrono::milliseconds delay, std::function<void()> task) = 0;
    virtual void cancel(TimerId timer) = 0;
    virtual std::chrono::steady_clock::time_point now() const = 0;

    virtual void writeClipboard(std::u16string_view text) = 0;
    virtual std::optional<std::u16string> readClipboard() = 0;

    // Time between two clicks that makes them a double click, and how far apart they may be.
    virtual std::chrono::milliseconds doubleClickInterval() const { return std::chrono::milliseconds(400); }
};

} // namespace brulion::atrament
