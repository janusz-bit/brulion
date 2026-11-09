// HabitCache (see habitcache.h).
#include "habits/habitcache.h"

#include <QSet>

#include <algorithm>

namespace brulion::habits {

bool HabitCache::sync(const VaultIndex &index)
{
    const auto compute = [](const VaultPath &path, const ParsedNote &note) {
        return NoteHabits{definitionsOf(path, note), checksOf(note)};
    };
    const auto keep = [](const NoteHabits &v) { return !v.definitions.isEmpty() || !v.checks.isEmpty(); };
    const bool first = !m_synced;
    m_synced = true;
    m_lastSync = m_notes.sync(index, QString(), false, compute, keep);
    if (!m_lastSync.any() && !first)
        return false;
    const QList<Habit> before = m_habits;
    const QHash<QString, DayMarks> daysBefore = m_days;
    rebuild();
    return first || before != m_habits || daysBefore != m_days;
}

void HabitCache::rebuild()
{
    m_habits.clear();
    m_byKey.clear();
    QHash<QString, DayMarks> days;
    for (const auto &[path, value] : m_notes.values()) {
        for (Habit h : value.definitions) {
            if (m_byKey.contains(h.key)) {
                h.duplicate = true;
            } else {
                m_byKey.insert(h.key, m_habits.size());
            }
            m_habits.append(std::move(h));
        }
        for (const Check &c : value.checks) {
            DayMarks &marks = days[c.key];
            const auto it = marks.find(c.date);
            if (it == marks.end())
                marks.insert(c.date, c.mark);
            else
                *it = strongerMark(*it, c.mark);
        }
    }
    // Revisions: a key's definition or days differ from the last rebuild's.
    QSet<QString> keys;
    for (auto it = days.cbegin(); it != days.cend(); ++it)
        keys.insert(it.key());
    for (auto it = m_days.cbegin(); it != m_days.cend(); ++it)
        keys.insert(it.key());
    QHash<QString, Habit> firsts;
    for (auto it = m_byKey.cbegin(); it != m_byKey.cend(); ++it) {
        firsts.insert(it.key(), m_habits[it.value()]);
        keys.insert(it.key());
    }
    for (auto it = m_previous.cbegin(); it != m_previous.cend(); ++it)
        keys.insert(it.key());
    for (const QString &key : std::as_const(keys)) {
        if (days.value(key) != m_days.value(key) || firsts.value(key) != m_previous.value(key)
            || firsts.contains(key) != m_previous.contains(key))
            ++m_revisions[key];
    }
    m_days = std::move(days);
    m_previous = std::move(firsts);
}

const Habit *HabitCache::find(QStringView name) const
{
    const auto it = m_byKey.constFind(habitKey(name));
    return it == m_byKey.constEnd() ? nullptr : &m_habits[*it];
}

QList<Check> HabitCache::checksOn(QDate date) const
{
    QList<Check> out;
    for (const auto &[path, value] : m_notes.values()) {
        for (const Check &c : value.checks) {
            if (c.date == date)
                out.append(c);
        }
    }
    return out;
}

QStringList HabitCache::undefinedKeys() const
{
    QStringList out;
    for (auto it = m_days.cbegin(); it != m_days.cend(); ++it) {
        if (!m_byKey.contains(it.key()))
            out.append(it.key());
    }
    std::ranges::sort(out);
    return out;
}

void HabitCache::clear()
{
    m_notes.clear();
    m_synced = false;
    m_lastSync = {};
    m_habits.clear();
    m_byKey.clear();
    m_days.clear();
    m_revisions.clear();
    m_previous.clear();
}

} // namespace brulion::habits
