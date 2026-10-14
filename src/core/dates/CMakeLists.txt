brulion_add_core_module(dates
    SOURCES
        dates.h
        local.h local.cpp
        names.h names.cpp
        natural.h natural.cpp natural_p.h natural_matchers.cpp
        recurrence.h recurrence.cpp
        expr.h expr.cpp
        format.h format.cpp
        vocabulary_p.h vocabulary.cpp
        protect_p.h protect.cpp
    DEPS text)
