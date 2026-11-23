# Scheduled conversations' rules (brulion_core_schedule): Polish and English phrases (rules with a
# time, intervals, single runs, what is refused), the rules in words, next runs and the catch-up
# of missed ones, daylight-saving edges in local wall-clock time, the kept JSON (malformed rows
# left out).
brulion_add_test(tst_schedule SOURCES tst_schedule.cpp LIBS brulion_core_schedule)
