# periodic — weekly (ISO week) and monthly notes, GUI-free and without the vault (docs/SYNTAX.md §1
# "Weekly and monthly notes"): ISO week arithmetic, file names both ways with the vault's formats,
# the phrases a user types ("ten tydzień", "2026-W45", "listopad 2026"), labels. Creating a note
# from its template: notebook/periodicnotes.h.
brulion_add_core_module(periodic
    SOURCES
        periodic.h periodic.cpp
    DEPS util text dates)
