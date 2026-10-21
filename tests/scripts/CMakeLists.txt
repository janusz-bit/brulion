# Tests of the shell scripts in scripts/: bash and git in throwaway repositories, no network.
find_program(BRULION_BASH bash)
find_package(Git QUIET)
if(BRULION_BASH AND GIT_FOUND)
    add_test(NAME tst_scripts_release
             COMMAND ${BRULION_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/tst_release.sh
                     ${PROJECT_SOURCE_DIR}/scripts/release.sh)
endif()
