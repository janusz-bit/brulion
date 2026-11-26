#include "ui/app.h"

#include "text/fonts.h"
#include "text/textsystem.h"
#include "ui/window.h"

#include <algorithm>

namespace brulion::atrament {

namespace {
constexpr std::uint32_t slotIndex(EntityId id)
{
    return std::uint32_t(id & 0xffffffffu);
}
constexpr std::uint32_t slotGeneration(EntityId id)
{
    return std::uint32_t(id >> 32);
}
constexpr EntityId makeEntityId(std::uint32_t index, std::uint32_t generation)
{
    return (EntityId(generation) << 32) | EntityId(index);
}
} // namespace

// --- Subscription

Subscription::Subscription(App *app, std::weak_ptr<int> alive, std::uint64_t id)
    : m_app(app)
    , m_alive(std::move(alive))
    , m_id(id)
{
}

Subscription::Subscription(Subscription &&o) noexcept
    : m_app(std::exchange(o.m_app, nullptr))
    , m_alive(std::move(o.m_alive))
    , m_id(std::exchange(o.m_id, 0))
{
}

Subscription &Subscription::operator=(Subscription &&o) noexcept
{
    if (this != &o) {
        Subscription old(std::move(*this));
        m_app = std::exchange(o.m_app, nullptr);
        m_alive = std::move(o.m_alive);
        m_id = std::exchange(o.m_id, 0);
    }
    return *this;
}

Subscription::~Subscription()
{
    if (m_app && m_id && !m_alive.expired())
        m_app->unsubscribe(m_id);
}

void Subscription::detach()
{
    m_app = nullptr;
    m_id = 0;
}

// --- FocusHandle

bool FocusHandle::isFocused(const Window &window) const
{
    return m_state && window.isFocused(m_state->id);
}

bool FocusHandle::containsFocus(const Window &window) const
{
    return m_state && window.focusWithin(m_state->id);
}

void FocusHandle::focus(Window &window) const
{
    if (m_state)
        window.focus(*this);
}

// --- ViewElement

NodeId ViewElement::requestLayout(Window &window, App &app)
{
    window.recordView(m_view.id);
    if (m_view.render)
        m_child = m_view.render(app, m_view.id, window);
    if (m_child.isEmpty())
        return window.requestLayout(LayoutStyle {}, {});
    return m_child.requestLayout(window, app);
}

void ViewElement::prepaint(Rect, Window &window, App &app)
{
    if (!m_child.isEmpty())
        m_child.prepaint(window, app);
}

void ViewElement::paint(Rect, Window &window, App &app)
{
    if (!m_child.isEmpty())
        m_child.paint(window, app);
}

// --- App

App::App(std::shared_ptr<Platform> platform, std::shared_ptr<FontCollection> fonts)
    : m_platform(std::move(platform))
    , m_textSystem(std::make_unique<TextSystem>(fonts ? std::move(fonts) : std::make_shared<FontCollection>()))
{
    m_slots.emplace_back(); // index 0 is never used: EntityId 0 means none
}

App::~App()
{
    m_destroying = true;
    m_windows.clear();
    for (auto &[id, timer] : m_timers)
        m_platform->cancel(timer);
    m_timers.clear();
    // Subscriptions held by entities see the token gone and leave the maps alone.
    m_alive.reset();
    // Entities may hold handles to each other: destroy them while handles still find their slots.
    for (Slot &s : m_slots) {
        std::unique_ptr<AnyEntityState> state = std::move(s.state);
        state.reset();
    }
    m_slots.clear();
}

EntityId App::insertEntity(std::unique_ptr<AnyEntityState> state)
{
    std::uint32_t index;
    if (!m_freeSlots.empty()) {
        index = m_freeSlots.back();
        m_freeSlots.pop_back();
    } else {
        index = std::uint32_t(m_slots.size());
        m_slots.emplace_back();
    }
    Slot &s = m_slots[index];
    s.state = std::move(state);
    s.strong = 1;
    return makeEntityId(index, s.generation);
}

App::Slot *App::slot(EntityId id)
{
    const std::uint32_t index = slotIndex(id);
    if (index == 0 || index >= m_slots.size())
        return nullptr;
    Slot &s = m_slots[index];
    return s.generation == slotGeneration(id) ? &s : nullptr;
}

const App::Slot *App::slot(EntityId id) const
{
    const std::uint32_t index = slotIndex(id);
    if (index == 0 || index >= m_slots.size())
        return nullptr;
    const Slot &s = m_slots[index];
    return s.generation == slotGeneration(id) ? &s : nullptr;
}

bool App::isAlive(EntityId id) const
{
    const Slot *s = slot(id);
    return s && s->strong > 0;
}

AnyEntityState *App::state(EntityId id) const
{
    const Slot *s = slot(id);
    return s ? s->state.get() : nullptr;
}

std::size_t App::entityCount() const
{
    std::size_t n = 0;
    for (const Slot &s : m_slots)
        n += s.state ? 1 : 0;
    return n;
}

void App::retain(EntityId id)
{
    if (m_destroying)
        return;
    if (Slot *s = slot(id))
        ++s->strong;
}

void App::release(EntityId id)
{
    if (m_destroying)
        return;
    Slot *s = slot(id);
    if (!s || s->strong == 0)
        return;
    if (--s->strong == 0) {
        m_dropped.push_back(id);
        if (m_updateDepth == 0)
            update([] { });
    }
}

void App::notify(EntityId id)
{
    m_notified.push_back(id);
    if (m_updateDepth == 0)
        update([] { });
}

Subscription App::makeSubscription(std::uint64_t id)
{
    return Subscription(this, m_alive, id);
}

Subscription App::observe(EntityId target, std::function<void(App &)> callback)
{
    const std::uint64_t id = m_nextSubscription++;
    m_observers.emplace(id, Observer { target, std::move(callback) });
    return makeSubscription(id);
}

Subscription App::onRelease(EntityId target, std::function<void(App &)> callback)
{
    const std::uint64_t id = m_nextSubscription++;
    m_releaseObservers.emplace(id, Observer { target, std::move(callback) });
    return makeSubscription(id);
}

void App::unsubscribe(std::uint64_t id)
{
    m_observers.erase(id);
    m_subscribers.erase(id);
    m_releaseObservers.erase(id);
    auto timer = m_timers.find(id);
    if (timer != m_timers.end()) {
        m_platform->cancel(timer->second);
        m_timers.erase(timer);
    }
}

void App::subscribeTimer(std::uint64_t subscription, TimerId timer)
{
    m_timers[subscription] = timer;
}

Subscription App::setTimeout(std::chrono::milliseconds delay, std::function<void(App &)> f)
{
    const std::uint64_t id = m_nextSubscription++;
    std::weak_ptr<int> alive = m_alive;
    const TimerId timer = m_platform->postDelayed(delay, [this, alive, id, f = std::move(f)] {
        if (alive.expired())
            return;
        m_timers.erase(id);
        update([&] { f(*this); });
    });
    m_timers[id] = timer;
    return makeSubscription(id);
}

void App::defer(std::function<void(App &)> f)
{
    m_deferred.push_back(std::move(f));
    if (m_updateDepth == 0 && !m_deferPosted) {
        m_deferPosted = true;
        std::weak_ptr<int> alive = m_alive;
        m_platform->post([this, alive] {
            if (alive.expired())
                return;
            m_deferPosted = false;
            update([] { });
        });
    }
}

FocusHandle App::newFocusHandle()
{
    FocusHandle h;
    h.m_state = std::make_shared<const FocusHandle::State>(FocusHandle::State { m_nextFocus++ });
    return h;
}

Window &App::openWindow(PlatformWindow &host, AnyView root)
{
    m_windows.push_back(std::make_unique<Window>(*this, host, root));
    return *m_windows.back();
}

void App::closeWindow(Window &window)
{
    auto it = std::find_if(m_windows.begin(), m_windows.end(), [&](const auto &w) { return w.get() == &window; });
    if (it == m_windows.end())
        return;
    // Never while it dispatches or draws: a listener of its own may be closing it.
    std::unique_ptr<Window> closing = std::move(*it);
    m_windows.erase(it);
    Window *raw = closing.release();
    defer([raw](App &) { delete raw; });
}

void App::refreshWindows()
{
    for (const auto &w : m_windows)
        w->refresh();
}

void App::endUpdate()
{
    if (--m_updateDepth > 0)
        return;
    if (m_flushing)
        return;
    m_flushing = true;
    ++m_updateDepth; // effects run inside an update of their own
    flushEffects();
    --m_updateDepth;
    m_flushing = false;
}

void App::flushEffects()
{
    // Effects can cause more effects; loop until nothing is left (bounded, against runaway loops).
    for (int round = 0; round < 1000; ++round) {
        if (m_notified.empty() && m_events.empty() && m_dropped.empty() && m_deferred.empty())
            break;
        if (!m_notified.empty()) {
            std::vector<EntityId> notified;
            notified.swap(m_notified);
            std::sort(notified.begin(), notified.end());
            notified.erase(std::unique(notified.begin(), notified.end()), notified.end());
            for (EntityId id : notified) {
                // Copy: an observer may subscribe or unsubscribe others.
                std::vector<std::function<void(App &)>> callbacks;
                for (const auto &[sid, o] : m_observers) {
                    if (o.target == id)
                        callbacks.push_back(o.callback);
                }
                for (auto &cb : callbacks)
                    cb(*this);
                for (const auto &w : m_windows) {
                    if (w->drewEntity(id))
                        w->refresh();
                }
            }
        }
        if (!m_events.empty()) {
            std::vector<PendingEvent> events;
            events.swap(m_events);
            for (const PendingEvent &e : events) {
                std::vector<std::function<void(const void *, App &)>> callbacks;
                for (const auto &[sid, s] : m_subscribers) {
                    if (s.emitter == e.emitter && s.type == e.type)
                        callbacks.push_back(s.callback);
                }
                for (auto &cb : callbacks)
                    cb(e.event.get(), *this);
            }
        }
        if (!m_deferred.empty()) {
            std::vector<std::function<void(App &)>> deferred;
            deferred.swap(m_deferred);
            for (auto &f : deferred)
                f(*this);
        }
        if (!m_dropped.empty()) {
            std::vector<EntityId> dropped;
            dropped.swap(m_dropped);
            for (EntityId id : dropped) {
                Slot *s = slot(id);
                if (!s || s->strong > 0)
                    continue; // revived meanwhile
                std::vector<std::function<void(App &)>> callbacks;
                for (auto it = m_releaseObservers.begin(); it != m_releaseObservers.end();) {
                    if (it->second.target == id) {
                        callbacks.push_back(std::move(it->second.callback));
                        it = m_releaseObservers.erase(it);
                    } else {
                        ++it;
                    }
                }
                for (auto &cb : callbacks)
                    cb(*this);
                std::unique_ptr<AnyEntityState> state = std::move(s->state);
                const std::uint32_t index = slotIndex(id);
                ++m_slots[index].generation;
                if (m_slots[index].generation == 0)
                    m_slots[index].generation = 1;
                m_freeSlots.push_back(index);
                state.reset(); // may release more handles: they land in m_dropped
            }
        }
    }
}

} // namespace brulion::atrament
