# Tests of the reminders module (brulion_core_reminders), one executable per area; reminderstest.h
# holds the shared fixtures, the recording sink and the controllable clock.
foreach(_t notifications scheduler calendar snooze cache)
    brulion_add_test(tst_reminders_${_t} SOURCES tst_reminders_${_t}.cpp reminderstest.h
        LIBS brulion_core_reminders)
endforeach()
