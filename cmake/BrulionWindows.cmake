# brulion_windows_resources(<target> <description>) — on Windows, the executable's resources
# (packaging/windows/brulion.rc.in): the icon packaging/windows/brulion.ico and the version
# information from the project version. Nothing elsewhere. Packaging: docs/WINDOWS.md.
function(brulion_windows_resources target description)
    if(NOT WIN32)
        return()
    endif()
    set(BRULION_WINDOWS_ICON "${PROJECT_SOURCE_DIR}/packaging/windows/brulion.ico")
    get_target_property(_output_name ${target} OUTPUT_NAME)
    if(_output_name)
        set(BRULION_WINDOWS_NAME "${_output_name}")
    else()
        set(BRULION_WINDOWS_NAME "${target}")
    endif()
    set(BRULION_WINDOWS_DESCRIPTION "${description}")
    set(_rc "${CMAKE_CURRENT_BINARY_DIR}/${target}.rc")
    configure_file("${PROJECT_SOURCE_DIR}/packaging/windows/brulion.rc.in" "${_rc}" @ONLY)
    target_sources(${target} PRIVATE "${_rc}")
endfunction()
