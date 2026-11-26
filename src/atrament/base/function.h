#pragma once

// Callables of a frame: `Fn<R(Args...)>` keeps its target in the current frame arena, so a
// listener costs a pointer bump, never a heap allocation. It is valid until the arena's reset —
// what must outlive the frame (an observer, a timer) uses std::function instead.

#include "base/arena.h"

#include <cassert>
#include <type_traits>
#include <utility>

namespace brulion::atrament {

template<class Signature>
class Fn;

template<class R, class... Args>
class Fn<R(Args...)> {
public:
    Fn() = default;
    Fn(std::nullptr_t) { }

    template<class F,
            class = std::enable_if_t<!std::is_same_v<std::decay_t<F>, Fn> && std::is_invocable_r_v<R, F &, Args...>>>
    Fn(F &&f)
    {
        using D = std::decay_t<F>;
        Arena *arena = Arena::current();
        assert(arena && "Fn needs a frame arena (Arena::current())");
        m_object = arena->make<D>(std::forward<F>(f));
        m_call = [](void *o, Args... args) -> R { return (*static_cast<D *>(o))(std::forward<Args>(args)...); };
    }

    R operator()(Args... args) const { return m_call(m_object, std::forward<Args>(args)...); }
    explicit operator bool() const { return m_call != nullptr; }

private:
    void *m_object = nullptr;
    R (*m_call)(void *, Args...) = nullptr;
};

} // namespace brulion::atrament
