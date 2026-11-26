#pragma once

// The frame arena (docs/ATRAMENT.md D4): a bump allocator whose memory lives until reset().
// A frame's elements, their children's vectors and their listeners are allocated here; reset()
// runs the destructors of what needed one (in reverse order) and keeps the chunks for the next
// frame, so a steady UI allocates nothing from the heap per frame.
//
// `Arena::current()` is the arena of the frame being built on this thread (set by a Window with
// an `ArenaScope`); element builders such as `div()` allocate from it without a context.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace brulion::atrament {

class Arena {
public:
    explicit Arena(std::size_t chunkSize = 64 * 1024);
    ~Arena();
    Arena(const Arena &) = delete;
    Arena &operator=(const Arena &) = delete;

    void *allocate(std::size_t size, std::size_t align);

    template<class T, class... Args>
    T *make(Args &&...args)
    {
        void *p = allocate(sizeof(T), alignof(T));
        T *obj = new (p) T(std::forward<Args>(args)...);
        if constexpr (!std::is_trivially_destructible_v<T>)
            registerDestructor(obj, [](void *o) { static_cast<T *>(o)->~T(); });
        return obj;
    }

    // Runs the registered destructors, newest first, and rewinds; the chunks stay.
    void reset();

    // Bytes handed out since the last reset (for tests and statistics).
    std::size_t used() const { return m_used; }
    std::size_t capacity() const;

    static Arena *current();

private:
    friend class ArenaScope;
    struct Chunk {
        std::unique_ptr<std::byte[]> data;
        std::size_t size = 0;
    };
    struct Destructor {
        void *object;
        void (*destroy)(void *);
    };
    void registerDestructor(void *object, void (*destroy)(void *));

    std::size_t m_chunkSize;
    std::vector<Chunk> m_chunks;
    std::size_t m_chunk = 0; // index of the chunk being filled
    std::size_t m_offset = 0; // in that chunk
    std::size_t m_used = 0;
    std::vector<Destructor> m_destructors;
};

// Makes an arena the current one of this thread for its lifetime (nests).
class ArenaScope {
public:
    explicit ArenaScope(Arena &arena);
    ~ArenaScope();
    ArenaScope(const ArenaScope &) = delete;
    ArenaScope &operator=(const ArenaScope &) = delete;

private:
    Arena *m_previous;
};

// A standard allocator over the current arena (falls back to the heap when there is none, so a
// container built outside a frame still works; such memory is freed normally).
template<class T>
class ArenaAllocator {
public:
    using value_type = T;
    ArenaAllocator() noexcept
        : m_arena(Arena::current())
    {
    }
    template<class U>
    ArenaAllocator(const ArenaAllocator<U> &o) noexcept
        : m_arena(o.arena())
    {
    }
    T *allocate(std::size_t n)
    {
        if (n > std::numeric_limits<std::size_t>::max() / sizeof(T))
            throw std::bad_array_new_length();
        if (m_arena)
            return static_cast<T *>(m_arena->allocate(n * sizeof(T), alignof(T)));
        return static_cast<T *>(::operator new(n * sizeof(T), std::align_val_t(alignof(T))));
    }
    void deallocate(T *p, std::size_t) noexcept
    {
        if (!m_arena)
            ::operator delete(p, std::align_val_t(alignof(T)));
    }
    Arena *arena() const noexcept { return m_arena; }
    template<class U>
    bool operator==(const ArenaAllocator<U> &o) const noexcept
    {
        return m_arena == o.arena();
    }

private:
    Arena *m_arena;
};

template<class T>
using ArenaVector = std::vector<T, ArenaAllocator<T>>;

} // namespace brulion::atrament
