# Tests of the sync module (brulion_core_sync). The in-memory WebDAV server and the shared
# helpers (synctest.h) live in a small support library linked into every test.
qt_add_library(brulion_sync_testsupport STATIC fakewebdavserver.h fakewebdavserver.cpp synctest.h)
target_link_libraries(brulion_sync_testsupport PUBLIC brulion_core_sync Qt6::Test)
target_include_directories(brulion_sync_testsupport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
brulion_set_warnings(brulion_sync_testsupport)

foreach(_t url xml diff merge cardmerge webdav engine devices scheduler syncthing massdelete history secrets folders processes
        folder streamed)
    brulion_add_test(tst_sync_${_t} SOURCES tst_sync_${_t}.cpp LIBS brulion_sync_testsupport)
endforeach()

# The same tests with every file other than a note synced in pieces (SyncEngine::setStreamAbove(0),
# the in-memory server streaming): downloads into staging files, uploads from the files.
foreach(_t engine devices folders massdelete)
    add_test(NAME tst_sync_${_t}_streamed COMMAND tst_sync_${_t})
    get_test_property(tst_sync_${_t} ENVIRONMENT _environment)
    set_tests_properties(tst_sync_${_t}_streamed PROPERTIES ENVIRONMENT "${_environment};BRULION_TEST_SYNC_STREAM=1")
endforeach()
