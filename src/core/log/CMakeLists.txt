# log — Brulion's event log ("Rejestr zdarzeń"): the logging categories, a chained Qt message
# handler with a thread-safe ring buffer and a rotating file, redaction of secrets, verbosity.
# QtCore only. Public headers: log.h (overview), categories.h, entry.h, redact.h, logger.h,
# slowscope.h. Internal: logfile_p.h (the rotating file sink).
brulion_add_core_module(log
    SOURCES
        log.h
        categories.h categories.cpp
        entry.h entry.cpp
        redact.h redact.cpp
        logger.h logger.cpp
        logfile_p.h logfile.cpp
        slowscope.h slowscope.cpp)
