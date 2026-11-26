#pragma once

// The app (docs/ATRAMENT.md "Views and entities"): it owns every entity — the state of views and
// models — and the windows, the text system and the atlas, and runs the effects of updates
// (notifications, events, releases) when an update ends.
//
//   auto counter = app.newEntity<Counter>();          // Entity<Counter>: a counted handle
//   counter.update(app, [](Counter &c, Context<Counter> &cx) { ++c.count; cx.notify(); });
//   const Counter &c = counter.read(app);
//
// A type with `AnyElement render(Window &, Context<T> &)` is a view: its entity converts to an
// element. `cx.notify()` makes every window that drew the entity draw again.
//
// Handles must not outlive the App. Entities whose handles are all gone are destroyed when the
// current update ends (never in the middle of one). Hold other entities weakly (WeakEntity)
// where they could hold you back, or the cycle stays alive until the App ends.
//
// One thread: the App and everything it owns belong to the thread that made it.

#include "scene/atlas.h"
#include "ui/element.h"
#include "ui/keymap.h"
#include "ui/platform.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace brulion::atrament {

class App;
class Window;
class TextSystem;
class FontCollection;
template<class T>
class Context;

using EntityId = std::uint64_t; // slot index in the low 32 bits, generation in the high 32
constexpr EntityId kNoEntity = 0;

class AnyEntityState {
public:
    virtual ~AnyEntityState() = default;
    virtual std::type_index type() const = 0;
};

template<class T>
class EntityState final : public AnyEntityState {
public:
    template<class... A>
    explicit EntityState(A &&...args)
        : value(std::forward<A>(args)...)
    {
    }
    std::type_index type() const override { return typeid(T); }
    T value;
};

// RAII: unsubscribes when destroyed.
class Subscription {
public:
    Subscription() = default;
    Subscription(Subscription &&o) noexcept;
    Subscription &operator=(Subscription &&o) noexcept;
    Subscription(const Subscription &) = delete;
    Subscription &operator=(const Subscription &) = delete;
    ~Subscription();
    // Keeps it for the App's lifetime (or the observed entity's).
    void detach();

private:
    friend class App;
    Subscription(App *app, std::weak_ptr<int> alive, std::uint64_t id);
    App *m_app = nullptr;
    std::weak_ptr<int> m_alive;
    std::uint64_t m_id = 0;
};

template<class T>
class WeakEntity;

template<class T>
concept Renderable = requires(T &t, Window &w, Context<T> &cx) {
    { t.render(w, cx) } -> std::convertible_to<AnyElement>;
};

template<class T>
class Entity {
public:
    Entity() = default;
    Entity(const Entity &o);
    Entity(Entity &&o) noexcept
        : m_app(std::exchange(o.m_app, nullptr))
        , m_id(std::exchange(o.m_id, kNoEntity))
    {
    }
    Entity &operator=(Entity o) noexcept
    {
        std::swap(m_app, o.m_app);
        std::swap(m_id, o.m_id);
        return *this;
    }
    ~Entity();

    EntityId id() const { return m_id; }
    explicit operator bool() const { return m_id != kNoEntity; }
    bool operator==(const Entity &o) const { return m_id == o.m_id; }

    const T &read(const App &app) const;
    // Runs f(T &, Context<T> &) and returns what it returns.
    template<class F>
    decltype(auto) update(App &app, F &&f) const;
    WeakEntity<T> downgrade() const;
    // A view's entity is an element (ui/element.h's AnyElement takes it).
    AnyElement intoElement() const
        requires Renderable<T>;

private:
    friend class App;
    template<class U>
    friend class WeakEntity;
    template<class U>
    friend class Context;
    Entity(App *app, EntityId id); // takes a new strong reference
    App *m_app = nullptr;
    EntityId m_id = kNoEntity;
};

template<class T>
class WeakEntity {
public:
    WeakEntity() = default;
    EntityId id() const { return m_id; }
    std::optional<Entity<T>> upgrade() const;
    // Runs f(T &, Context<T> &) if the entity is still alive; returns whether it was.
    template<class F>
    bool update(App &app, F &&f) const;

private:
    friend class Entity<T>;
    template<class U>
    friend class Context;
    WeakEntity(App *app, EntityId id)
        : m_app(app)
        , m_id(id)
    {
    }
    App *m_app = nullptr;
    EntityId m_id = kNoEntity;
};

// A view of any type: an entity that renders.
struct AnyView {
    EntityId id = kNoEntity;
    AnyElement (*render)(App &, EntityId, Window &) = nullptr;
    bool isValid() const { return id != kNoEntity && render; }
};

// Draws a view: renders it in its entity's context and lays out, prepaints and paints what it
// returned, under the view's identity (ElementId::number(entity)).
class ViewElement final : public Element {
public:
    explicit ViewElement(AnyView view)
        : m_view(view)
    {
    }
    NodeId requestLayout(Window &window, App &app) override;
    void prepaint(Rect bounds, Window &window, App &app) override;
    void paint(Rect bounds, Window &window, App &app) override;
    ElementId id() const override { return ElementId::number(m_view.id); }

private:
    AnyView m_view;
    AnyElement m_child;
};

template<class T>
AnyElement renderView(App &app, EntityId id, Window &window);

// Focus: a handle an element tracks (`div().trackFocus(handle)`); the window knows which one has
// the focus. Copies share the identity.
class FocusHandle {
public:
    FocusHandle() = default;
    std::uint64_t id() const { return m_state ? m_state->id : 0; }
    explicit operator bool() const { return bool(m_state); }
    bool operator==(const FocusHandle &o) const { return id() == o.id(); }
    bool isFocused(const Window &window) const;
    // This handle or an element inside the one tracking it has the focus.
    bool containsFocus(const Window &window) const;
    void focus(Window &window) const;

private:
    friend class App;
    struct State {
        std::uint64_t id;
    };
    std::shared_ptr<const State> m_state;
};

class App {
public:
    // `fonts` may be shared between Apps (tests); nullptr: an empty collection.
    explicit App(std::shared_ptr<Platform> platform, std::shared_ptr<FontCollection> fonts = nullptr);
    ~App();
    App(const App &) = delete;
    App &operator=(const App &) = delete;

    Platform &platform() { return *m_platform; }
    TextSystem &textSystem() { return *m_textSystem; }
    Atlas &atlas() { return m_atlas; }
    Keymap &keymap() { return m_keymap; }

    // --- entities
    template<class T, class... A>
    Entity<T> newEntity(A &&...args);
    bool isAlive(EntityId id) const;
    template<class T>
    T *tryGet(EntityId id);
    template<class T>
    const T *tryGet(EntityId id) const;
    // f(T &, Context<T> &) inside an update; nullopt-like false when the entity is gone.
    template<class T, class F>
    bool updateEntity(EntityId id, F &&f);

    // Marks the entity changed: its observers run and the windows that drew it draw again, when
    // the update ends.
    void notify(EntityId id);
    template<class E>
    void emit(EntityId emitter, const E &event);
    [[nodiscard]] Subscription observe(EntityId target, std::function<void(App &)> callback);
    template<class E>
    [[nodiscard]] Subscription subscribe(EntityId emitter, std::function<void(const E &, App &)> callback);
    // Runs when the entity is released.
    [[nodiscard]] Subscription onRelease(EntityId target, std::function<void(App &)> callback);

    // --- windows
    // Attaches a window to a platform window the host owns; the host detaches it (closeWindow)
    // before destroying the platform window.
    Window &openWindow(PlatformWindow &host, AnyView root);
    template<class T>
    Window &openWindow(PlatformWindow &host, const Entity<T> &root);
    void closeWindow(Window &window);
    const std::vector<std::unique_ptr<Window>> &windows() const { return m_windows; }
    void refreshWindows();

    // --- focus, timers, the clipboard
    FocusHandle newFocusHandle();
    // Runs `f` after `delay` (in an update); cancelled when the returned subscription goes.
    [[nodiscard]] Subscription setTimeout(std::chrono::milliseconds delay, std::function<void(App &)> f);
    // Runs `f` when the current update ends (or soon, outside one).
    void defer(std::function<void(App &)> f);
    void writeToClipboard(std::u16string_view text) { m_platform->writeClipboard(text); }
    std::optional<std::u16string> readFromClipboard() { return m_platform->readClipboard(); }

    // Every change to entities happens inside an update: effects run when the outermost ends.
    template<class F>
    decltype(auto) update(F &&f);

    // Statistics for tests.
    std::size_t entityCount() const;

    // Internal (used by Entity, Window, elements).
    void retain(EntityId id);
    void release(EntityId id);
    AnyEntityState *state(EntityId id) const;
    void beginUpdate() { ++m_updateDepth; }
    void endUpdate();
    void subscribeTimer(std::uint64_t subscription, TimerId timer);
    void unsubscribe(std::uint64_t id);

private:
    struct Slot {
        std::unique_ptr<AnyEntityState> state;
        std::uint32_t generation = 1;
        std::uint32_t strong = 0;
    };
    struct Observer {
        EntityId target;
        std::function<void(App &)> callback;
    };
    struct Subscriber {
        EntityId emitter;
        std::type_index type;
        std::function<void(const void *, App &)> callback;
    };
    struct PendingEvent {
        EntityId emitter;
        std::type_index type;
        std::shared_ptr<void> event;
    };
    EntityId insertEntity(std::unique_ptr<AnyEntityState> state);
    Slot *slot(EntityId id);
    const Slot *slot(EntityId id) const;
    void flushEffects();
    Subscription makeSubscription(std::uint64_t id);

    std::shared_ptr<int> m_alive = std::make_shared<int>(0);
    std::shared_ptr<Platform> m_platform;
    std::unique_ptr<TextSystem> m_textSystem;
    Atlas m_atlas;
    Keymap m_keymap;
    std::vector<Slot> m_slots;
    std::vector<std::uint32_t> m_freeSlots;
    std::vector<EntityId> m_dropped;
    std::vector<EntityId> m_notified;
    std::vector<PendingEvent> m_events;
    std::vector<std::function<void(App &)>> m_deferred;
    std::unordered_map<std::uint64_t, Observer> m_observers;
    std::unordered_map<std::uint64_t, Subscriber> m_subscribers;
    std::unordered_map<std::uint64_t, Observer> m_releaseObservers;
    std::unordered_map<std::uint64_t, TimerId> m_timers;
    std::uint64_t m_nextSubscription = 1;
    std::uint64_t m_nextFocus = 1;
    int m_updateDepth = 0;
    bool m_flushing = false;
    bool m_destroying = false;
    bool m_deferPosted = false;
    std::vector<std::unique_ptr<Window>> m_windows;
};

// The context of an entity's update: the App and the entity's identity.
template<class T>
class Context {
public:
    Context(App &app, EntityId id)
        : m_app(app)
        , m_id(id)
    {
    }
    App &app() const { return m_app; }
    EntityId entityId() const { return m_id; }
    Entity<T> entity() const;
    WeakEntity<T> weakEntity() const { return WeakEntity<T>(&m_app, m_id); }
    void notify() { m_app.notify(m_id); }
    template<class E>
    void emit(const E &event)
    {
        m_app.emit(m_id, event);
    }

    // A listener for an element's events that calls back into this entity:
    //   onClick(cx.listener([](T &self, const ClickEvent &, Window &, Context<T> &cx) { … }))
    // Holds the entity weakly; does nothing once it is gone.
    template<class F>
    auto listener(F &&f) const;

    // f(T &, Context<T> &) whenever `other` notifies.
    template<class U, class F>
    [[nodiscard]] Subscription observe(const Entity<U> &other, F &&f);
    // f(T &, const E &, Context<T> &) for every E `other` emits.
    template<class E, class U, class F>
    [[nodiscard]] Subscription subscribe(const Entity<U> &other, F &&f);
    // f(T &, Context<T> &) after `delay`.
    template<class F>
    [[nodiscard]] Subscription setTimeout(std::chrono::milliseconds delay, F &&f);

    template<class U, class... A>
    Entity<U> newEntity(A &&...args)
    {
        return m_app.newEntity<U>(std::forward<A>(args)...);
    }
    FocusHandle newFocusHandle() { return m_app.newFocusHandle(); }

private:
    App &m_app;
    EntityId m_id;
};

// --- implementation of the templates

template<class T>
Entity<T>::Entity(App *app, EntityId id)
    : m_app(app)
    , m_id(id)
{
    if (m_app && m_id)
        m_app->retain(m_id);
}

template<class T>
Entity<T>::Entity(const Entity &o)
    : m_app(o.m_app)
    , m_id(o.m_id)
{
    if (m_app && m_id)
        m_app->retain(m_id);
}

template<class T>
Entity<T>::~Entity()
{
    if (m_app && m_id)
        m_app->release(m_id);
}

template<class T>
const T &Entity<T>::read(const App &app) const
{
    const T *t = app.tryGet<T>(m_id);
    assert(t && "Entity::read of a released entity");
    return *t;
}

template<class T>
template<class F>
decltype(auto) Entity<T>::update(App &app, F &&f) const
{
    return app.update([&]() -> decltype(auto) {
        T *t = app.tryGet<T>(m_id);
        assert(t && "Entity::update of a released entity");
        Context<T> cx(app, m_id);
        return f(*t, cx);
    });
}

template<class T>
WeakEntity<T> Entity<T>::downgrade() const
{
    return WeakEntity<T>(m_app, m_id);
}

template<class T>
std::optional<Entity<T>> WeakEntity<T>::upgrade() const
{
    if (!m_app || !m_app->isAlive(m_id))
        return std::nullopt;
    return Entity<T>(m_app, m_id);
}

template<class T>
template<class F>
bool WeakEntity<T>::update(App &app, F &&f) const
{
    return app.updateEntity<T>(m_id, std::forward<F>(f));
}

template<class T, class... A>
Entity<T> App::newEntity(A &&...args)
{
    EntityId id;
    if constexpr (std::is_constructible_v<T, Context<T> &, A...>) {
        // The entity's id is known before it is constructed, so its constructor gets a context.
        id = insertEntity(nullptr);
        Context<T> cx(*this, id);
        slot(id)->state = std::make_unique<EntityState<T>>(cx, std::forward<A>(args)...);
    } else {
        id = insertEntity(std::make_unique<EntityState<T>>(std::forward<A>(args)...));
    }
    Entity<T> handle(this, id);
    release(id); // insertEntity counted one reference: the handle holds it now
    return handle;
}

template<class T>
T *App::tryGet(EntityId id)
{
    AnyEntityState *s = state(id);
    if (!s || s->type() != std::type_index(typeid(T)))
        return nullptr;
    return &static_cast<EntityState<T> *>(s)->value;
}

template<class T>
const T *App::tryGet(EntityId id) const
{
    AnyEntityState *s = state(id);
    if (!s || s->type() != std::type_index(typeid(T)))
        return nullptr;
    return &static_cast<const EntityState<T> *>(s)->value;
}

template<class T, class F>
bool App::updateEntity(EntityId id, F &&f)
{
    T *t = tryGet<T>(id);
    if (!t)
        return false;
    update([&] {
        Context<T> cx(*this, id);
        f(*t, cx);
    });
    return true;
}

template<class F>
decltype(auto) App::update(F &&f)
{
    struct Guard {
        App &app;
        ~Guard() { app.endUpdate(); }
    };
    beginUpdate();
    Guard guard { *this };
    return f();
}

template<class E>
void App::emit(EntityId emitter, const E &event)
{
    m_events.push_back(PendingEvent { emitter, std::type_index(typeid(E)), std::make_shared<E>(event) });
    if (m_updateDepth == 0)
        update([] { });
}

template<class E>
Subscription App::subscribe(EntityId emitter, std::function<void(const E &, App &)> callback)
{
    const std::uint64_t id = m_nextSubscription++;
    m_subscribers.emplace(id,
            Subscriber { emitter, std::type_index(typeid(E)),
                    [cb = std::move(callback)](const void *e, App &app) { cb(*static_cast<const E *>(e), app); } });
    return makeSubscription(id);
}

template<class T>
AnyElement renderView(App &app, EntityId id, Window &window)
{
    T *t = app.tryGet<T>(id);
    if (!t)
        return {};
    Context<T> cx(app, id);
    return t->render(window, cx);
}

template<class T>
AnyElement Entity<T>::intoElement() const
    requires Renderable<T>
{
    return ViewElement(AnyView { m_id, &renderView<T> });
}

template<class T>
Window &App::openWindow(PlatformWindow &host, const Entity<T> &root)
{
    static_assert(Renderable<T>, "a window's root must be a view (T::render(Window &, Context<T> &))");
    return openWindow(host, AnyView { root.id(), &renderView<T> });
}

template<class T>
Entity<T> Context<T>::entity() const
{
    return Entity<T>(&m_app, m_id);
}

namespace detail {

template<class T, class F>
struct EntityListener {
    App *app;
    EntityId id;
    F f;
    template<class E>
        requires std::invocable<F &, T &, const E &, Window &, Context<T> &>
    void operator()(const E &event, Window &window, App &) const
    {
        app->updateEntity<T>(id, [&](T &self, Context<T> &cx) { const_cast<F &>(f)(self, event, window, cx); });
    }
    void operator()(Window &window, App &) const
        requires std::invocable<F &, T &, Window &, Context<T> &>
    {
        app->updateEntity<T>(id, [&](T &self, Context<T> &cx) { const_cast<F &>(f)(self, window, cx); });
    }
};

} // namespace detail

template<class T>
template<class F>
auto Context<T>::listener(F &&f) const
{
    return detail::EntityListener<T, std::decay_t<F>> { &m_app, m_id, std::forward<F>(f) };
}

template<class T>
template<class U, class F>
Subscription Context<T>::observe(const Entity<U> &other, F &&f)
{
    App *app = &m_app;
    const EntityId self = m_id;
    return m_app.observe(other.id(), [app, self, f = std::forward<F>(f)](App &) mutable {
        app->updateEntity<T>(self, [&](T &t, Context<T> &cx) { f(t, cx); });
    });
}

template<class T>
template<class E, class U, class F>
Subscription Context<T>::subscribe(const Entity<U> &other, F &&f)
{
    App *app = &m_app;
    const EntityId self = m_id;
    return m_app.subscribe<E>(other.id(), [app, self, f = std::forward<F>(f)](const E &e, App &) mutable {
        app->updateEntity<T>(self, [&](T &t, Context<T> &cx) { f(t, e, cx); });
    });
}

template<class T>
template<class F>
Subscription Context<T>::setTimeout(std::chrono::milliseconds delay, F &&f)
{
    App *app = &m_app;
    const EntityId self = m_id;
    return m_app.setTimeout(delay, [app, self, f = std::forward<F>(f)](App &) mutable {
        app->updateEntity<T>(self, [&](T &t, Context<T> &cx) { f(t, cx); });
    });
}

} // namespace brulion::atrament
