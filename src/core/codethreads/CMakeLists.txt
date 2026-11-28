# codethreads — a Code mode thread's place in the sidebar (docs/CODE.md "Settling and snoozing"):
# settled, snoozed, auto-settle in its page's frontmatter, read and written as pure edits; the
# section the sidebar lists it in; snooze presets and times in words. GUI-free and pure.
brulion_add_core_module(codethreads
    SOURCES
        codethreads.h codethreads.cpp
    DEPS markdown dates chat)
