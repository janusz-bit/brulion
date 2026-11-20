# Tests of the publish module (brulion_core_publish): which pages a target publishes, the site and
# its search, plans (the dry run), the store, and publishing against in-memory servers — GitHub
# (../github/fakegithub.h, with GitHub Pages), WebDAV (../sync/fakewebdavserver.h) and Nextcloud
# with its OCS Share API (fakenextcloud.h) — and into a folder. No network.
qt_add_library(brulion_publish_testsupport STATIC fakenextcloud.h fakenextcloud.cpp publishtest.h)
target_link_libraries(brulion_publish_testsupport
    PUBLIC brulion_core_publish brulion_sync_testsupport brulion_github_testsupport Qt6::Test brulion_test_timing)
target_include_directories(brulion_publish_testsupport
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/tests/common)
brulion_set_warnings(brulion_publish_testsupport)

foreach(_t selection github webdav)
    brulion_add_test(tst_publish_${_t} SOURCES tst_publish_${_t}.cpp LIBS brulion_publish_testsupport)
endforeach()
