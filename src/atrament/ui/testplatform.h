#pragma once

// The test platform (docs/ATRAMENT.md "Testing"): windows without a screen, a virtual clock, input
// made up in process, frames rendered by the CPU renderer. Deterministic: nothing depends on
// wall-clock time or on another thread.
//
//   auto platform = std::make_shared<TestPlatform>();
//   App app(platform, fonts);
//   TestWindow &tw = platform->newWindow({ 400, 300 });
//   Window &w = app.openWindow(tw, app.newEntity<Counter>());
//   tw.draw();
//   tw.simulateClick({ 20, 20 });          // dispatches, then draws if a frame was asked for
//   QCOMPARE(tw.pixel(20, 20), …);

#include "raster/raster.h"
#include "ui/accessibility.h"
#include "ui/platform.h"

#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace brulion::atrament {

class TestPlatform;

class TestWindow final : public PlatformWindow {
public:
    TestWindow(TestPlatform &platform, Size size, float scale);

    Size contentSize() const override { return m_size; }
    float scaleFactor() const override { return m_scale; }
    Point mousePosition() const override { return m_mouse; }
    Modifiers modifiers() const override { return m_modifiers; }
    bool isActive() const override { return m_active; }
    void requestFrame() override { m_frameRequested = true; }
    void present(const Scene &scene, Atlas &atlas) override;
    void setCursor(CursorStyle cursor) override { m_cursor = cursor; }
    void setTitle(std::u16string_view title) override { m_title = title; }
    void inputStateChanged() override { ++m_inputStateChanges; }
    void accessibilityChanged(const std::vector<AccessibilityEvent> &events) override;

    // Runs queued tasks and draws while a frame is asked for; returns the number of frames drawn.
    int settle();
    // Draws one frame now.
    void draw();
    bool frameRequested() const { return m_frameRequested; }
    int framesPresented() const { return m_frames; }

    void resize(Size size);
    void setScaleFactor(float scale);
    void setActive(bool active);
    // Rendering pixels costs time: tests of logic can turn it off (the scene is still built).
    void setRasterize(bool on) { m_rasterize = on; }

    // Input; each settles afterwards. Return whether the window handled the event.
    bool simulateMouseMove(Point p, std::optional<MouseButton> pressed = std::nullopt);
    bool simulateMouseDown(Point p, MouseButton button = MouseButton::Left, Modifiers modifiers = {}, int clickCount = 1);
    bool simulateMouseUp(Point p, MouseButton button = MouseButton::Left, Modifiers modifiers = {}, int clickCount = 1);
    // Move, press and release.
    bool simulateClick(Point p, MouseButton button = MouseButton::Left, Modifiers modifiers = {}, int clickCount = 1);
    bool simulateScroll(Point p, Point delta, Modifiers modifiers = {});
    // "ctrl-a backspace": a key down and up for each, keyChar from the key where it types one.
    bool simulateKeystrokes(std::u16string_view keystrokes);
    // Text typed key by key (one key down per code point, with its character as keyChar).
    void simulateTyping(std::u16string_view text);
    // Input-method composition through the focused element's input handler.
    bool simulateComposition(std::u16string_view preedit);
    bool simulateCommit(std::u16string_view text);
    void simulateMouseExit();

    const RasterTarget &image() const { return m_target; }
    Pixel pixel(int x, int y) const; // device pixels
    CursorStyle cursor() const { return m_cursor; }
    const std::u16string &title() const { return m_title; }
    int inputStateChanges() const { return m_inputStateChanges; }
    InputHandler *inputHandler() const;
    const Scene *lastScene() const { return m_lastScene; }
    // The accessibility events of the frames drawn since the last call (with copies of their text).
    struct RecordedAccessibilityEvent {
        AccessibilityEvent::Type type;
        AccessNodeId node = 0;
        AccessNodeId parent = 0;
        AccessibilityStates changed;
        std::u16string oldText;
    };
    std::vector<RecordedAccessibilityEvent> takeAccessibilityEvents() { return std::exchange(m_accessEvents, {}); }

private:
    bool send(const PlatformInput &input);

    TestPlatform &m_platform;
    Size m_size;
    float m_scale;
    Point m_mouse { -1e9f, -1e9f };
    Modifiers m_modifiers;
    bool m_active = true;
    bool m_frameRequested = false;
    bool m_rasterize = true;
    int m_frames = 0;
    int m_inputStateChanges = 0;
    CursorStyle m_cursor = CursorStyle::Arrow;
    std::u16string m_title;
    RasterTarget m_target;
    RasterRenderer m_renderer;
    const Scene *m_lastScene = nullptr;
    std::vector<RecordedAccessibilityEvent> m_accessEvents;
};

class TestPlatform final : public Platform {
public:
    TestPlatform();
    ~TestPlatform() override;

    std::unique_ptr<PlatformWindow> openWindow(const WindowOptions &options) override;
    // A window owned by the platform (alive as long as it is).
    TestWindow &newWindow(Size size, float scale = 1);

    void post(std::function<void()> task) override;
    TimerId postDelayed(std::chrono::milliseconds delay, std::function<void()> task) override;
    void cancel(TimerId timer) override;
    std::chrono::steady_clock::time_point now() const override { return m_now; }
    void writeClipboard(std::u16string_view text) override { m_clipboard = std::u16string(text); }
    std::optional<std::u16string> readClipboard() override { return m_clipboard; }

    // Runs posted tasks (and the tasks they post) without moving the clock.
    void runUntilParked();
    // Moves the clock, running each timer when its time comes (in order).
    void advanceClock(std::chrono::milliseconds by);
    std::size_t pendingTimers() const { return m_timers.size(); }

private:
    std::chrono::steady_clock::time_point m_now;
    std::deque<std::function<void()>> m_tasks;
    struct Timer {
        std::chrono::steady_clock::time_point due;
        std::function<void()> task;
    };
    std::map<TimerId, Timer> m_timers;
    TimerId m_nextTimer = 1;
    std::optional<std::u16string> m_clipboard;
    std::vector<std::unique_ptr<TestWindow>> m_windows;
};

} // namespace brulion::atrament
