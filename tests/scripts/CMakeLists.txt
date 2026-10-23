# Tests of the shell scripts in scripts/: bash and git in throwaway repositories, no network.
find_program(BRULION_BASH bash)
find_package(Git QUIET)
if(BRULION_BASH AND GIT_FOUND)
    add_test(NAME tst_scripts_release
             COMMAND ${BRULION_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/tst_release.sh
                     ${PROJECT_SOURCE_DIR}/scripts/release.sh)
endif()
# A binding follows what it reads for real (docs/UI.md): scripts/check-qml-reads.sh on small QML
# files, then on the app's QML — no bare `void x` read, which qmlcachegen compiles away together
# with the binding's dependency.
if(BRULION_BASH)
    add_test(NAME tst_scripts_check_qml_reads
             COMMAND ${BRULION_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/tst_check_qml_reads.sh
                     ${PROJECT_SOURCE_DIR}/scripts/check-qml-reads.sh ${PROJECT_SOURCE_DIR}/src/app/qml)
endif()
# Stand-in AppImages are ELF files made with objcopy; the script reads them with readelf.
find_program(BRULION_OBJCOPY objcopy)
find_program(BRULION_READELF readelf)
if(BRULION_BASH AND BRULION_OBJCOPY AND BRULION_READELF)
    add_test(NAME tst_scripts_appimage_update_check
             COMMAND ${BRULION_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/tst_appimage_update_check.sh
                     ${PROJECT_SOURCE_DIR}/scripts/appimage-update-check.sh)
endif()
