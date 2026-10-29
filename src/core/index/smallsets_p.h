// Compact sets and counters of vault paths for the index's lookup maps. Internal.
//
// Nearly every key of the index's maps (a link key, a block id, a tag, a page name) names one or
// two files, and Qt 6's QSet/QHash allocate a span of 48 entries (~1.4 KB for a QSet<QString>) on
// their first insert: a 10 000-note vault spent tens of MB on nearly empty spans
// (docs/PERFORMANCE.md, L8). Up to SmallLimit paths live in a sorted QList (~60 bytes for one);
// more in a QSet/QHash (a page linked from thousands of notes), where a bulk insert in random order
// stays O(1) per path instead of the O(n) of a sorted insert.
#pragma once

#include "types.h"

#include <QHash>
#include <QList>
#include <QSet>

#include <algorithm>
#include <initializer_list>
#include <utility>

namespace brulion::index_detail {

/// The vault paths filed under one key of an index map (see the file comment).
class PathSet {
public:
    static constexpr qsizetype SmallLimit = 32;

    PathSet() = default;
    PathSet(std::initializer_list<VaultPath> paths)
    {
        for (const VaultPath &p : paths)
            insert(p);
    }

    /// Add `path`; true when it was not there.
    bool insert(const VaultPath &path)
    {
        if (!m_big.isEmpty()) {
            const qsizetype before = m_big.size();
            m_big.insert(path);
            return m_big.size() != before;
        }
        const auto pos = std::lower_bound(m_small.begin(), m_small.end(), path);
        if (pos != m_small.end() && *pos == path)
            return false;
        if (m_small.size() < SmallLimit) {
            m_small.insert(pos, path);
            return true;
        }
        m_big.reserve(m_small.size() * 2);
        for (const VaultPath &p : std::as_const(m_small))
            m_big.insert(p);
        m_big.insert(path);
        m_small = {};
        return true;
    }

    /// Remove `path`; true when it was there. A big set shrinks back to the list at
    /// SmallLimit / 2 paths (not at SmallLimit: no flapping at the border).
    bool remove(const VaultPath &path)
    {
        if (!m_big.isEmpty()) {
            if (!m_big.remove(path))
                return false;
            if (m_big.size() <= SmallLimit / 2) {
                m_small = QList<VaultPath>(m_big.cbegin(), m_big.cend());
                std::sort(m_small.begin(), m_small.end());
                m_big = {};
            }
            return true;
        }
        const auto pos = std::lower_bound(m_small.begin(), m_small.end(), path);
        if (pos == m_small.end() || *pos != path)
            return false;
        m_small.erase(pos);
        if (m_small.isEmpty())
            m_small = {};
        return true;
    }

    bool contains(const VaultPath &path) const
    {
        if (!m_big.isEmpty())
            return m_big.contains(path);
        return std::binary_search(m_small.cbegin(), m_small.cend(), path);
    }
    qsizetype size() const { return m_big.isEmpty() ? m_small.size() : m_big.size(); }
    bool isEmpty() const { return m_small.isEmpty() && m_big.isEmpty(); }

    /// The paths sorted by code units.
    QList<VaultPath> sorted() const
    {
        if (m_big.isEmpty())
            return m_small;
        QList<VaultPath> out(m_big.cbegin(), m_big.cend());
        std::sort(out.begin(), out.end());
        return out;
    }

    /// f(path) for every path, in any order.
    template<class F>
    void forEach(F &&f) const
    {
        if (m_big.isEmpty()) {
            for (const VaultPath &p : m_small)
                f(p);
        } else {
            for (const VaultPath &p : m_big)
                f(p);
        }
    }

    /// The path smallest by `less` (a strict weak order); empty when the set is empty.
    template<class Less>
    VaultPath min(Less &&less) const
    {
        const VaultPath *best = nullptr;
        forEach([&](const VaultPath &p) {
            if (!best || less(p, *best))
                best = &p;
        });
        return best ? *best : VaultPath();
    }

    friend bool operator==(const PathSet &a, const PathSet &b)
    {
        return a.size() == b.size() && a.sorted() == b.sorted();
    }

    /// Iteration in any order (the list's or the set's), like QSet's.
    class const_iterator {
    public:
        const VaultPath &operator*() const { return m_big ? *m_set : *m_list; }
        const VaultPath *operator->() const { return &**this; }
        const_iterator &operator++()
        {
            if (m_big)
                ++m_set;
            else
                ++m_list;
            return *this;
        }
        friend bool operator==(const const_iterator &a, const const_iterator &b)
        {
            return a.m_big ? a.m_set == b.m_set : a.m_list == b.m_list;
        }

    private:
        friend class PathSet;
        QList<VaultPath>::const_iterator m_list{};
        QSet<VaultPath>::const_iterator m_set{};
        bool m_big = false;
    };
    const_iterator begin() const { return makeIterator(true); }
    const_iterator end() const { return makeIterator(false); }
    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }

private:
    const_iterator makeIterator(bool first) const
    {
        const_iterator it;
        it.m_big = !m_big.isEmpty();
        if (it.m_big)
            it.m_set = first ? m_big.cbegin() : m_big.cend();
        else
            it.m_list = first ? m_small.cbegin() : m_small.cend();
        return it;
    }

    QList<VaultPath> m_small; ///< sorted (code units) while m_big is empty
    QSet<VaultPath> m_big;    ///< every path once there were more than SmallLimit
};

/// Path -> positive count (the links of a source to a target, a tag's occurrences in a note), with
/// PathSet's layout: a list sorted by path up to SmallLimit paths, a QHash above.
class PathCounts {
public:
    static constexpr qsizetype SmallLimit = PathSet::SmallLimit;

    /// Add 1 to the count of `path`.
    void add(const VaultPath &path) { adjust(path, 1, false); }
    /// Set the count of `path` (> 0; 0 or less removes it).
    void set(const VaultPath &path, int count)
    {
        if (count <= 0)
            remove(path);
        else
            adjust(path, count, true);
    }
    /// Subtract 1 from the count of `path`, dropping it at zero; true when it was dropped.
    bool subtract(const VaultPath &path)
    {
        if (!m_big.isEmpty()) {
            const auto it = m_big.find(path);
            if (it == m_big.end())
                return false;
            if (--*it > 0)
                return false;
            m_big.erase(it);
            shrink();
            return true;
        }
        const auto pos = lowerBound(path);
        if (pos == m_small.end() || pos->first != path)
            return false;
        if (--pos->second > 0)
            return false;
        m_small.erase(pos);
        if (m_small.isEmpty())
            m_small = {};
        return true;
    }
    /// Drop `path` whatever its count; true when it was there.
    bool remove(const VaultPath &path)
    {
        if (!m_big.isEmpty()) {
            if (!m_big.remove(path))
                return false;
            shrink();
            return true;
        }
        const auto pos = lowerBound(path);
        if (pos == m_small.end() || pos->first != path)
            return false;
        m_small.erase(pos);
        if (m_small.isEmpty())
            m_small = {};
        return true;
    }

    /// The count of `path`, 0 when absent.
    int value(const VaultPath &path) const
    {
        if (!m_big.isEmpty())
            return m_big.value(path);
        const auto pos = std::lower_bound(m_small.cbegin(), m_small.cend(), path,
                                          [](const Item &i, const VaultPath &p) { return i.first < p; });
        return pos != m_small.cend() && pos->first == path ? pos->second : 0;
    }
    bool contains(const VaultPath &path) const { return value(path) > 0; }
    qsizetype size() const { return m_big.isEmpty() ? m_small.size() : m_big.size(); }
    bool isEmpty() const { return m_small.isEmpty() && m_big.isEmpty(); }

    /// The paths sorted by code units.
    QList<VaultPath> keys() const
    {
        QList<VaultPath> out;
        out.reserve(size());
        forEach([&](const VaultPath &p, int) { out.append(p); });
        if (!m_big.isEmpty())
            std::sort(out.begin(), out.end());
        return out;
    }

    /// f(path, count) for every path, in any order.
    template<class F>
    void forEach(F &&f) const
    {
        if (m_big.isEmpty()) {
            for (const Item &i : m_small)
                f(i.first, i.second);
        } else {
            for (auto it = m_big.cbegin(); it != m_big.cend(); ++it)
                f(it.key(), it.value());
        }
    }

    /// Iteration in any order, like QHash's: key() is the path, value() its count.
    class const_iterator {
    public:
        const VaultPath &key() const { return m_big ? m_hash.key() : m_list->first; }
        int value() const { return m_big ? m_hash.value() : m_list->second; }
        int operator*() const { return value(); }
        const_iterator &operator++()
        {
            if (m_big)
                ++m_hash;
            else
                ++m_list;
            return *this;
        }
        friend bool operator==(const const_iterator &a, const const_iterator &b)
        {
            return a.m_big ? a.m_hash == b.m_hash : a.m_list == b.m_list;
        }

    private:
        friend class PathCounts;
        QList<std::pair<VaultPath, int>>::const_iterator m_list{};
        QHash<VaultPath, int>::const_iterator m_hash{};
        bool m_big = false;
    };
    const_iterator begin() const { return makeIterator(true); }
    const_iterator end() const { return makeIterator(false); }
    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }

    friend bool operator==(const PathCounts &a, const PathCounts &b)
    {
        if (a.size() != b.size())
            return false;
        bool same = true;
        a.forEach([&](const VaultPath &p, int count) { same = same && b.value(p) == count; });
        return same;
    }

private:
    using Item = std::pair<VaultPath, int>;

    const_iterator makeIterator(bool first) const
    {
        const_iterator it;
        it.m_big = !m_big.isEmpty();
        if (it.m_big)
            it.m_hash = first ? m_big.cbegin() : m_big.cend();
        else
            it.m_list = first ? m_small.cbegin() : m_small.cend();
        return it;
    }

    QList<Item>::iterator lowerBound(const VaultPath &path)
    {
        return std::lower_bound(m_small.begin(), m_small.end(), path,
                                [](const Item &i, const VaultPath &p) { return i.first < p; });
    }

    void adjust(const VaultPath &path, int count, bool replace)
    {
        if (!m_big.isEmpty()) {
            int &value = m_big[path];
            value = replace ? count : value + count;
            return;
        }
        const auto pos = lowerBound(path);
        if (pos != m_small.end() && pos->first == path) {
            pos->second = replace ? count : pos->second + count;
            return;
        }
        if (m_small.size() < SmallLimit) {
            m_small.insert(pos, Item(path, count));
            return;
        }
        m_big.reserve(m_small.size() * 2);
        for (const Item &i : std::as_const(m_small))
            m_big.insert(i.first, i.second);
        m_big.insert(path, count);
        m_small = {};
    }

    void shrink()
    {
        if (m_big.size() > SmallLimit / 2)
            return;
        m_small.clear();
        m_small.reserve(m_big.size());
        for (auto it = m_big.cbegin(); it != m_big.cend(); ++it)
            m_small.append(Item(it.key(), it.value()));
        std::sort(m_small.begin(), m_small.end(), [](const Item &a, const Item &b) { return a.first < b.first; });
        m_big = {};
    }

    QList<Item> m_small;         ///< sorted by path while m_big is empty
    QHash<VaultPath, int> m_big; ///< every path once there were more than SmallLimit
};

/// Add `value` to the set stored under `key`.
template<typename K>
void multiAdd(QHash<K, PathSet> &map, const K &key, const VaultPath &value)
{
    map[key].insert(value);
}

/// Remove `value` from the set under `key`, dropping empty sets.
template<typename K>
void multiRemove(QHash<K, PathSet> &map, const K &key, const VaultPath &value)
{
    const auto it = map.find(key);
    if (it == map.end())
        return;
    it->remove(value);
    if (it->isEmpty())
        map.erase(it);
}

/// Add 1 to the count of `value` under `key`.
template<typename K>
void countAdd(QHash<K, PathCounts> &map, const K &key, const VaultPath &value)
{
    map[key].add(value);
}

/// Subtract 1 from the count of `value` under `key`, dropping zero counts and empty maps.
template<typename K>
void countRemove(QHash<K, PathCounts> &map, const K &key, const VaultPath &value)
{
    const auto it = map.find(key);
    if (it == map.end())
        return;
    it->subtract(value);
    if (it->isEmpty())
        map.erase(it);
}

} // namespace brulion::index_detail
