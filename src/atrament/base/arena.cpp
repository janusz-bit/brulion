#include "base/arena.h"

#include <algorithm>

namespace brulion::atrament {

namespace {
thread_local Arena *t_current = nullptr;
}

Arena::Arena(std::size_t chunkSize)
    : m_chunkSize(std::max<std::size_t>(chunkSize, 256))
{
}

Arena::~Arena()
{
    reset();
}

void *Arena::allocate(std::size_t size, std::size_t align)
{
    if (size == 0)
        size = 1;
    for (;;) {
        if (m_chunk < m_chunks.size()) {
            Chunk &c = m_chunks[m_chunk];
            const auto base = reinterpret_cast<std::uintptr_t>(c.data.get());
            const std::uintptr_t aligned = (base + m_offset + align - 1) & ~(std::uintptr_t(align) - 1);
            const std::size_t offset = aligned - base;
            if (offset + size <= c.size) {
                m_offset = offset + size;
                m_used += size;
                return c.data.get() + offset;
            }
            // Try the next chunk, if one is left from an earlier frame.
            if (m_chunk + 1 < m_chunks.size() && size + align <= m_chunks[m_chunk + 1].size) {
                ++m_chunk;
                m_offset = 0;
                continue;
            }
        }
        // A new chunk, at least big enough for this allocation, inserted after the current one.
        const std::size_t chunkSize = std::max(m_chunkSize, size + align);
        Chunk chunk { std::make_unique<std::byte[]>(chunkSize), chunkSize };
        if (m_chunks.empty()) {
            m_chunks.push_back(std::move(chunk));
            m_chunk = 0;
        } else {
            m_chunks.insert(m_chunks.begin() + std::ptrdiff_t(m_chunk + 1), std::move(chunk));
            ++m_chunk;
        }
        m_offset = 0;
    }
}

void Arena::registerDestructor(void *object, void (*destroy)(void *))
{
    m_destructors.push_back({ object, destroy });
}

void Arena::reset()
{
    // A destructor may itself allocate from or register with no arena (it is being torn down),
    // so take the list first.
    while (!m_destructors.empty()) {
        std::vector<Destructor> list;
        list.swap(m_destructors);
        for (auto it = list.rbegin(); it != list.rend(); ++it)
            it->destroy(it->object);
    }
    m_chunk = 0;
    m_offset = 0;
    m_used = 0;
}

std::size_t Arena::capacity() const
{
    std::size_t total = 0;
    for (const Chunk &c : m_chunks)
        total += c.size;
    return total;
}

Arena *Arena::current()
{
    return t_current;
}

ArenaScope::ArenaScope(Arena &arena)
    : m_previous(t_current)
{
    t_current = &arena;
}

ArenaScope::~ArenaScope()
{
    t_current = m_previous;
}

} // namespace brulion::atrament
