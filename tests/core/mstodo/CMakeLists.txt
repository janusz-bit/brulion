# Tests of the mstodo module (brulion_core_mstodo): the device-code sign-in and the Graph download
# against FakeMicrosoft, an in-memory identity platform + Graph To Do API (fakemicrosoft.h, also
# used by the app's MsTodoImport tests). No network; the download test feeds its bundle to the io
# module's importer.
qt_add_library(brulion_mstodo_testsupport STATIC fakemicrosoft.h fakemicrosoft.cpp)
target_link_libraries(brulion_mstodo_testsupport PUBLIC brulion_core_mstodo Qt6::Test)
target_include_directories(brulion_mstodo_testsupport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
brulion_set_warnings(brulion_mstodo_testsupport)

brulion_add_test(tst_mstodo_auth SOURCES tst_mstodo_auth.cpp LIBS brulion_mstodo_testsupport)
brulion_add_test(tst_mstodo_download SOURCES tst_mstodo_download.cpp
    LIBS brulion_mstodo_testsupport brulion_core_io)
