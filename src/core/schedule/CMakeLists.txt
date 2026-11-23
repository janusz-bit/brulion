# schedule — when a scheduled conversation of the AI chat runs (docs/UI.md "Scheduled
# conversations"): a rule in words (Polish and English, through the dates module), shown back in
# words, its next run in local wall-clock time and the catch-up of a run missed while the app was
# closed, and the schedules as the device keeps them (JSON). GUI-free; the app's runner:
# src/app/shell/chatschedules.h.
brulion_add_core_module(schedule
    SOURCES
        schedule.h schedule.cpp
    DEPS dates text)
