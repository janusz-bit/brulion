#pragma once

// Hashing for Atrament's caches and element ids: 64-bit FNV-1a over bytes, and a combiner.
// Not for anything security-related.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>

namespace brulion::atrament {

constexpr std::uint64_t kHashSeed = 0xcbf29ce484222325ull;

constexpr std::uint64_t hashBytes(const void *data, std::size_t size, std::uint64_t h = kHashSeed)
{
    const auto *p = static_cast<const unsigned char *>(data);
    for (std::size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

inline std::uint64_t hashString(std::u16string_view s, std::uint64_t h = kHashSeed)
{
    return hashBytes(s.data(), s.size() * sizeof(char16_t), h);
}

inline std::uint64_t hashString(std::string_view s, std::uint64_t h = kHashSeed)
{
    return hashBytes(s.data(), s.size(), h);
}

// Mixes a value into a running hash (splitmix64's finaliser over the xor).
constexpr std::uint64_t hashCombine(std::uint64_t h, std::uint64_t v)
{
    std::uint64_t x = h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2));
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ull;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebull;
    x ^= x >> 31;
    return x;
}

template<class T>
std::uint64_t hashValue(const T &v, std::uint64_t h = kHashSeed)
{
    static_assert(std::is_trivially_copyable_v<T>);
    return hashBytes(&v, sizeof(T), h);
}

inline std::uint64_t hashFloat(float f, std::uint64_t h = kHashSeed)
{
    if (f == 0)
        f = 0; // -0 and +0 alike
    std::uint32_t bits;
    std::memcpy(&bits, &f, sizeof bits);
    return hashCombine(h, bits);
}

} // namespace brulion::atrament
