# habits — recurring daily items with a streak and a month view, recorded in the journal
# (docs/SYNTAX.md §5 "Nawyki / Habits"): schedules (dates' recurrence parser where it fits), the
# definitions (`habit::` blocks, a page's property) and checks (#nawyk tasks of daily notes) of a
# note, streaks, completion rates and the month grid, and HabitCache over the index (per note,
# incremental). Public headers: habits.h (overview), habitcache.h.
brulion_add_core_module(habits
    SOURCES
        habits.h habits.cpp schedule.cpp
        habitcache.h habitcache.cpp
    DEPS text dates markdown index)
