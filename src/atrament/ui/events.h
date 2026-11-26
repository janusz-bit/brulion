#pragma once

// Input events (docs/ATRAMENT.md D10), in the window's logical pixels. A platform (the Qt host,
// later Wayland, Win32, Android, the browser) turns its own events into these; a test platform
// makes them up. Keys are named as Zed/GPUI names them ("a", "enter", "backspace", "left",
// "f5", "space"), modifiers as "ctrl", "alt", "shift", "super" (Windows / Command key), "fn".

#include "base/geometry.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace brulion::atrament {

enum class MouseButton : std::uint8_t { Left, Right, Middle, Back, Forward };
// A finger and a pen manipulate directly: their drags scroll (docs/ATRAMENT.md "Touch"); a mouse's
// drags select and drag.
enum class PointerKind : std::uint8_t { Mouse, Touch, Pen };

// When the platform saw an event, in milliseconds of its own input clock (only differences
// count: a finger's velocity is measured from them); zero when it does not say.
using EventTime = std::chrono::milliseconds;

struct Modifiers {
    bool control = false;
    bool alt = false;
    bool shift = false;
    bool platform = false; // Super / Windows / Command
    bool function = false;

    constexpr bool any() const { return control || alt || shift || platform || function; }
    // The modifier of shortcuts: Ctrl here (Command on macOS once Atrament runs there).
    constexpr bool secondary() const { return control; }
    constexpr bool operator==(const Modifiers &) const = default;
};

// One key press, e.g. ctrl-shift-a. `key` is the key's name in lower case; `keyChar` is the text it
// types with the current layout ("A" for shift-a, "ż" for alt-z on a Polish keyboard, empty for
// keys that type nothing).
struct Keystroke {
    Modifiers modifiers;
    std::u16string key;
    std::u16string keyChar;

    // "ctrl-shift-a", "alt-enter", "f5", "space", "-" (minus), "ctrl--" (ctrl and minus);
    // nullopt for an empty or malformed string.
    static std::optional<Keystroke> parse(std::u16string_view text);
    // A space-separated sequence: "ctrl-k ctrl-s".
    static std::vector<Keystroke> parseSequence(std::u16string_view text);
    std::u16string toString() const;
    // The same key and modifiers (keyChar is ignored).
    bool matches(const Keystroke &other) const { return modifiers == other.modifiers && key == other.key; }
};

struct MouseDownEvent {
    MouseButton button = MouseButton::Left;
    Point position;
    Modifiers modifiers;
    int clickCount = 1; // 2 for a double click, 3 for a triple click
    PointerKind pointer = PointerKind::Mouse;
    EventTime time { 0 };
};

struct MouseUpEvent {
    MouseButton button = MouseButton::Left;
    Point position;
    Modifiers modifiers;
    int clickCount = 1;
    PointerKind pointer = PointerKind::Mouse;
    EventTime time { 0 };
    // The press ended without a release: a finger's drag became a scroll, or the system took the
    // touch. Dispatched over nothing — no element is under it, nothing is clicked, pressed looks end.
    bool cancelled = false;
};

struct MouseMoveEvent {
    Point position;
    std::optional<MouseButton> pressedButton;
    Modifiers modifiers;
    PointerKind pointer = PointerKind::Mouse;
    EventTime time { 0 };
};

// The pointer left the window.
struct MouseExitEvent {
    Point position;
    Modifiers modifiers;
};

enum class TouchPhase : std::uint8_t { Started, Moved, Ended };

struct ScrollWheelEvent {
    Point position;
    // How far the content should move, in logical pixels (lines already converted); positive y
    // scrolls towards the end (the content moves up).
    Point delta;
    bool precise = false; // a touchpad or a touch screen: pixel deltas, kinetic phases
    TouchPhase phase = TouchPhase::Moved;
    Modifiers modifiers;
};

struct KeyDownEvent {
    Keystroke keystroke;
    bool isRepeat = false;
};

struct KeyUpEvent {
    Keystroke keystroke;
};

struct ModifiersChangedEvent {
    Modifiers modifiers;
};

using PlatformInput = std::variant<MouseDownEvent, MouseUpEvent, MouseMoveEvent, MouseExitEvent, ScrollWheelEvent,
        KeyDownEvent, KeyUpEvent, ModifiersChangedEvent>;

// A press and release of a button over the same element.
struct ClickEvent {
    MouseDownEvent down;
    MouseUpEvent up;
    int clickCount() const { return up.clickCount; }
    bool isKeyboard = false; // activated with Enter / Space on a focused element
    bool fromAccessibility = false; // activated by an assistive technology (a screen reader)
};

// Listeners run twice per event: Capture from the root down, then Bubble from the topmost element up.
enum class DispatchPhase : std::uint8_t { Capture, Bubble };

Point positionOf(const PlatformInput &input);
bool isMouseEvent(const PlatformInput &input);
bool isKeyEvent(const PlatformInput &input);

} // namespace brulion::atrament
