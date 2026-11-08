# Tests of the github module (brulion_core_github): repository addresses, Git object ids, the API
# client's errors, and RepoSync — pulls, commits and proposals — against FakeGitHub, an in-memory
# GitHub (fakegithub.h, also used by the app's tests). No network.
qt_add_library(brulion_github_testsupport STATIC fakegithub.h fakegithub.cpp)
target_link_libraries(brulion_github_testsupport PUBLIC brulion_core_github Qt6::Test)
target_include_directories(brulion_github_testsupport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
brulion_set_warnings(brulion_github_testsupport)

brulion_add_test(tst_github_address SOURCES tst_github_address.cpp LIBS brulion_github_testsupport)
brulion_add_test(tst_github_sync SOURCES tst_github_sync.cpp LIBS brulion_github_testsupport)
