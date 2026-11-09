// Following a VaultIndex across its versions (VaultIndex::changesSince()): IndexFollower tells a
// consumer what to compute again since it last looked, NoteCache keeps a computation's value per
// note and computes only the notes a change names. What QueryCache (query.h), CalendarCache
// (reminders/calendar.h) and NotificationCache (reminders/notifications.h) are built on. Part of
// the index module — see index/index.h. Header-only.
#pragma once

#include "index/vaultindex.h"

#include <QList>
#include <QSet>
#include <QString>

#include <functional>
#include <map>

namespace brulion {

/// What a follower has to compute again (IndexFollower::follow()).
struct NoteCacheSync {
    /// Everything: the first time, another index or key, a change log that does not reach back,
    /// or names changed for a consumer that depends on them.
    bool full = false;
    /// Otherwise the notes to compute again (sorted; notes removed since are among them).
    QList<VaultPath> changed;

    bool any() const { return full || !changed.isEmpty(); }
};

/// Keeps a consumer in step with a VaultIndex: follow() returns what changed since the previous
/// call. `key` stands for everything else the consumer's values depend on (a date range, a
/// query, `today`): another key means everything. `dependsOnNames`: the values read how names
/// resolve (links, `page:`, `ref:`), so a note or alias added, removed or renamed anywhere is
/// a full sync.
class IndexFollower {
public:
    NoteCacheSync follow(const VaultIndex &index, const QString &key, bool dependsOnNames)
    {
        NoteCacheSync sync;
        if (m_version < 0 || m_serial != index.serial() || m_key != key) {
            sync.full = true;
        } else if (m_version != index.version()) {
            const IndexChanges changes = index.changesSince(m_version);
            if (!changes.complete || (dependsOnNames && changes.names))
                sync.full = true;
            else
                sync.changed = changes.notes;
        }
        m_serial = index.serial();
        m_version = index.version();
        m_key = key;
        return sync;
    }
    /// Forget the index: the next follow() is a full sync.
    void reset() { *this = IndexFollower(); }

private:
    quint64 m_serial = 0;
    int m_version = -1;
    QString m_key;
};

/// Per-note values of a computation over the index, kept only for the notes whose value is worth
/// keeping (keep()). sync() brings them up to date: compute() runs for every note after a full
/// sync, else for the notes changed since the previous sync (removed ones are dropped).
template<typename Value>
class NoteCache {
public:
    using Compute = std::function<Value(const VaultPath &, const ParsedNote &)>;
    using Keep = std::function<bool(const Value &)>;
    /// Fills an empty map with every note's value worth keeping, in path order — what compute()
    /// and keep() would give note by note, from something cheaper to walk (the index's task or
    /// reminder list: contiguous, kept up to date by the index).
    using Fill = std::function<void(std::map<VaultPath, Value> &)>;

    /// Full syncs call `fill` (when given) instead of compute() for every note.
    NoteCacheSync sync(const VaultIndex &index, const QString &key, bool dependsOnNames, const Compute &compute,
                       const Keep &keep, const Fill &fill = Fill())
    {
        NoteCacheSync s = m_follower.follow(index, key, dependsOnNames);
        if (s.full) {
            m_values.clear();
            if (fill) {
                fill(m_values);
            } else {
                // Paths come in order: each value goes at the end, nothing to erase.
                index.forEachNote([&](const VaultPath &path, const ParsedNote &note) {
                    Value value = compute(path, note);
                    if (keep(value))
                        m_values.emplace_hint(m_values.end(), path, std::move(value));
                });
            }
            return s;
        }
        for (const VaultPath &path : std::as_const(s.changed)) {
            if (const ParsedNote *note = index.note(path)) {
                Value value = compute(path, *note);
                if (keep(value)) {
                    m_values.insert_or_assign(path, std::move(value));
                    continue;
                }
            }
            m_values.erase(path); // removed, or not worth keeping
        }
        return s;
    }

    /// The kept values by path (the order of VaultIndex::paths(): QString code units).
    const std::map<VaultPath, Value> &values() const { return m_values; }

    /// Forget everything (the next sync() is full).
    void clear() { *this = NoteCache(); }

private:
    IndexFollower m_follower;
    std::map<VaultPath, Value> m_values;
};

} // namespace brulion
