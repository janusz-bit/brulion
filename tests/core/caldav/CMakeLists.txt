# Tests of the caldav module (brulion_core_caldav): recurrence, time zones, events of feeds, ICS
# subscriptions, the CalDAV client and the two-way sync of tasks and events against FakeCalDav, an
# in-memory CalDAV server (fakecaldav.h), changes of events as intents (eventchange: fields, times,
# the changes they make, JSON, the inbox). No network.
qt_add_library(brulion_caldav_testsupport STATIC fakecaldav.h fakecaldav.cpp)
target_link_libraries(brulion_caldav_testsupport PUBLIC brulion_core_caldav Qt6::Test)
target_include_directories(brulion_caldav_testsupport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
brulion_set_warnings(brulion_caldav_testsupport)

foreach(_t rrule events alarms client taskmap tasks feed radicale eventchange)
    brulion_add_test(tst_caldav_${_t} SOURCES tst_caldav_${_t}.cpp LIBS brulion_caldav_testsupport)
endforeach()
