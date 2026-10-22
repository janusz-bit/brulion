# Tests of the shell scripts in scripts/: bash and git in throwaway repositories, no network.
find_program(BRULION_BASH bash)
find_package(Git QUIET)
if(BRULION_BASH AND GIT_FOUND)
    add_test(NAME tst_scripts_release
             COMMAND ${BRULION_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/tst_release.sh
                     ${PROJECT_SOURCE_DIR}/scripts/release.sh)
endif()
# Stand-in AppImages are ELF files made with objcopy; the script reads them with readelf.
find_program(BRULION_OBJCOPY objcopy)
find_program(BRULION_READELF readelf)
if(BRULION_BASH AND BRULION_OBJCOPY AND BRULION_READELF)
    add_test(NAME tst_scripts_appimage_update_check
             COMMAND ${BRULION_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/tst_appimage_update_check.sh
                     ${PROJECT_SOURCE_DIR}/scripts/appimage-update-check.sh)
endif()
