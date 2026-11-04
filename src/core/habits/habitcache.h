// HabitCache — every habit definition and check of a VaultIndex, kept per note across index
// changes (index/notecache.h): after a change only the notes it names are read again, and the
// merged lists are rebuilt from the kept per-note values (the notes defining habits and the daily
// notes with checks — never the whole vault). Part of the habits module — see habits/habits.h.
#pragma once

#include "habits/habits.h"
#include "index/notecache.h"

#include <QHash>
#include <QList>
#include <QString>

namespace brulion::habits {

class HabitCache {
public:
    /// Bring the cache up to date with `index`. True when anything it holds changed (always the
    /// first time).
    bool sync(const VaultIndex &index);

    /// Every definition, notes by path and in document order (a page's own first); a definition
    /// whose name an earlier one already has is kept with Habit::duplicate set.
    const QList<Habit> &habits() const { return m_habits; }
    /// The first definition named `name` (habitKey(); no duplicate), or nullptr.
    const Habit *find(QStringView name) const;
    /// Each day's mark of the habit with key `key` (habitKey()): its checks in every daily note,
    /// also checks of a name no definition has (yet).
    DayMarks days(const QString &key) const { return m_days.value(key); }
    /// The checks of day `date` (several daily notes of one day: all of them), in path order.
    QList<Check> checksOn(QDate date) const;
    /// Keys of the checks whose name no definition has, sorted.
    QStringList undefinedKeys() const;
    /// Grows whenever the habit's definition or its days change: a consumer keeping statistics
    /// per habit computes again only those whose revision moved.
    int revision(const QString &key) const { return m_revisions.value(key, 0); }
    /// What the last sync() read again (tests).
    const NoteCacheSync &lastSync() const { return m_lastSync; }
    /// Forget everything (the next sync() reads every note).
    void clear();

private:
    struct NoteHabits {
        QList<Habit> definitions;
        QList<Check> checks;
    };
    void rebuild();

    NoteCache<NoteHabits> m_notes;
    NoteCacheSync m_lastSync;
    bool m_synced = false;
    QList<Habit> m_habits;
    QHash<QString, qsizetype> m_byKey; ///< key -> index into m_habits (the first definition)
    QHash<QString, DayMarks> m_days;
    QHash<QString, int> m_revisions;
    QHash<QString, Habit> m_previous; ///< the first definition of each key at the last rebuild
};

} // namespace brulion::habits
