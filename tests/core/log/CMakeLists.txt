# Tests of the log module (brulion_core_log): redaction, entries and their text form, the Logger
# (handler chain, ring, collapse, rate limits, threads, verbosity, export, rotating file).
foreach(_t redact entry logger)
    brulion_add_test(tst_log_${_t} SOURCES tst_log_${_t}.cpp LIBS brulion_core_log)
endforeach()
