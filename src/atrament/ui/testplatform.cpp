#include "ui/testplatform.h"

#include "text/segmentation.h"

#include <algorithm>
#include <cmath>

namespace brulion::atrament {

// --- TestWindow

TestWindow::TestWindow(TestPlatform &platform, Size size, float scale)
    : m_platform(platform)
    , m_size(size)
    , m_scale(scale)
{
}

void TestWindow::present(const Scene &scene, Atlas &atlas)
{
    ++m_frames;
    m_lastScene = &scene;
    if (!m_rasterize)
        return;
    const int w = std::max(0, int(std::ceil(m_size.width * m_scale)));
    const int h = std::max(0, int(std::ceil(m_size.height * m_scale)));
    if (m_target.width != w || m_target.height != h)
        m_target.resize(w, h);
    m_renderer.render(scene, atlas, m_target, white());
}

void TestWindow::accessibilityChanged(const std::vector<AccessibilityEvent> &events)
{
    for (const AccessibilityEvent &e : events)
        m_accessEvents.push_back({e.type, e.node, e.parent, e.changed, std::u16string(e.oldText)});
}

void TestWindow::draw()
{
    m_frameRequested = false;
    if (m_callbacks.frame)
        m_callbacks.frame();
}

int TestWindow::settle()
{
    int frames = 0;
    for (int i = 0; i < 100; ++i) {
        m_platform.runUntilParked();
        if (!m_frameRequested)
            break;
        draw();
        ++frames;
    }
    return frames;
}

void TestWindow::resize(Size size)
{
    m_size = size;
    if (m_callbacks.resized)
        m_callbacks.resized();
    settle();
}

void TestWindow::setScaleFactor(float scale)
{
    m_scale = scale;
    if (m_callbacks.resized)
        m_callbacks.resized();
    settle();
}

void TestWindow::setActive(bool active)
{
    m_active = active;
    if (m_callbacks.activeChanged)
        m_callbacks.activeChanged(active);
    settle();
}

bool TestWindow::send(const PlatformInput &input)
{
    const bool handled = m_callbacks.input ? m_callbacks.input(input) : false;
    settle();
    return handled;
}

bool TestWindow::simulateMouseMove(Point p, std::optional<MouseButton> pressed)
{
    m_mouse = p;
    MouseMoveEvent e;
    e.position = p;
    e.pressedButton = pressed;
    e.modifiers = m_modifiers;
    return send(e);
}

bool TestWindow::simulateMouseDown(Point p, MouseButton button, Modifiers modifiers, int clickCount)
{
    m_mouse = p;
    MouseDownEvent e;
    e.position = p;
    e.button = button;
    e.modifiers = modifiers;
    e.clickCount = clickCount;
    return send(e);
}

bool TestWindow::simulateMouseUp(Point p, MouseButton button, Modifiers modifiers, int clickCount)
{
    m_mouse = p;
    MouseUpEvent e;
    e.position = p;
    e.button = button;
    e.modifiers = modifiers;
    e.clickCount = clickCount;
    return send(e);
}

bool TestWindow::simulateClick(Point p, MouseButton button, Modifiers modifiers, int clickCount)
{
    simulateMouseMove(p);
    const bool down = simulateMouseDown(p, button, modifiers, clickCount);
    const bool up = simulateMouseUp(p, button, modifiers, clickCount);
    return down || up;
}

bool TestWindow::simulateScroll(Point p, Point delta, Modifiers modifiers)
{
    m_mouse = p;
    ScrollWheelEvent e;
    e.position = p;
    e.delta = delta;
    e.modifiers = modifiers;
    return send(e);
}

void TestWindow::simulateMouseExit()
{
    m_mouse = { -1e9f, -1e9f };
    send(MouseExitEvent {});
}

bool TestWindow::simulateKeystrokes(std::u16string_view keystrokes)
{
    bool handled = false;
    for (const Keystroke &k : Keystroke::parseSequence(keystrokes)) {
        m_modifiers = k.modifiers;
        KeyDownEvent down;
        down.keystroke = k;
        handled = send(down) || handled;
        KeyUpEvent up;
        up.keystroke = k;
        send(up);
    }
    m_modifiers = {};
    return handled;
}

void TestWindow::simulateTyping(std::u16string_view text)
{
    int i = 0;
    while (i < int(text.size())) {
        const int start = i;
        readCodepoint(text, i);
        Keystroke k;
        k.keyChar = std::u16string(text.substr(std::size_t(start), std::size_t(i - start)));
        k.key = k.keyChar;
        if (k.key == u" ")
            k.key = u"space";
        else if (k.key == u"\n")
            k.key = u"enter";
        KeyDownEvent down;
        down.keystroke = k;
        send(down);
    }
}

InputHandler *TestWindow::inputHandler() const
{
    return m_callbacks.inputHandler ? m_callbacks.inputHandler() : nullptr;
}

bool TestWindow::simulateComposition(std::u16string_view preedit)
{
    InputHandler *h = inputHandler();
    if (!h)
        return false;
    h->replaceAndMarkText(std::nullopt, preedit, std::nullopt);
    settle();
    return true;
}

bool TestWindow::simulateCommit(std::u16string_view text)
{
    InputHandler *h = inputHandler();
    if (!h)
        return false;
    h->replaceText(std::nullopt, text);
    settle();
    return true;
}

Pixel TestWindow::pixel(int x, int y) const
{
    if (x < 0 || y < 0 || x >= m_target.width || y >= m_target.height)
        return {};
    return m_target.at(x, y);
}

// --- TestPlatform

TestPlatform::TestPlatform() = default;
TestPlatform::~TestPlatform() = default;

std::unique_ptr<PlatformWindow> TestPlatform::openWindow(const WindowOptions &options)
{
    return std::make_unique<TestWindow>(*this, options.size, 1);
}

TestWindow &TestPlatform::newWindow(Size size, float scale)
{
    m_windows.push_back(std::make_unique<TestWindow>(*this, size, scale));
    return *m_windows.back();
}

void TestPlatform::post(std::function<void()> task)
{
    m_tasks.push_back(std::move(task));
}

TimerId TestPlatform::postDelayed(std::chrono::milliseconds delay, std::function<void()> task)
{
    const TimerId id = m_nextTimer++;
    m_timers.emplace(id, Timer { m_now + std::max(delay, std::chrono::milliseconds(0)), std::move(task) });
    return id;
}

void TestPlatform::cancel(TimerId timer)
{
    m_timers.erase(timer);
}

void TestPlatform::runUntilParked()
{
    for (int guard = 0; guard < 100000 && !m_tasks.empty(); ++guard) {
        std::function<void()> task = std::move(m_tasks.front());
        m_tasks.pop_front();
        task();
    }
}

void TestPlatform::advanceClock(std::chrono::milliseconds by)
{
    const auto target = m_now + by;
    for (;;) {
        runUntilParked();
        // The earliest due timer (ties in the order they were set).
        auto next = m_timers.end();
        for (auto it = m_timers.begin(); it != m_timers.end(); ++it) {
            if (it->second.due <= target && (next == m_timers.end() || it->second.due < next->second.due))
                next = it;
        }
        if (next == m_timers.end())
            break;
        m_now = std::max(m_now, next->second.due);
        std::function<void()> task = std::move(next->second.task);
        m_timers.erase(next);
        task();
    }
    m_now = target;
    runUntilParked();
}

} // namespace brulion::atrament
