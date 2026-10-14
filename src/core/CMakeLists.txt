# brulion_core — GUI-free logic shared by every target (Linux, Android, WebAssembly).
#
# Each module lives in src/core/<module>/ with its own CMakeLists.txt calling
#   brulion_add_core_module(<module> SOURCES ... [DEPS <other modules>] [QT <extra Qt components>])
# which creates the static library `brulion_core_<module>`. Module directories are picked up
# automatically, so modules never edit this file. `brulion_core` aggregates all modules.
add_library(brulion_core INTERFACE)
target_include_directories(brulion_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

function(brulion_add_core_module name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPS;QT" ${ARGN})
    qt_add_library(brulion_core_${name} STATIC ${ARG_SOURCES})
    target_include_directories(brulion_core_${name} PUBLIC ${CMAKE_CURRENT_FUNCTION_LIST_DIR})
    target_link_libraries(brulion_core_${name} PUBLIC Qt6::Core)
    foreach(_dep IN LISTS ARG_DEPS)
        target_link_libraries(brulion_core_${name} PUBLIC brulion_core_${_dep})
    endforeach()
    foreach(_qt IN LISTS ARG_QT)
        target_link_libraries(brulion_core_${name} PUBLIC Qt6::${_qt})
    endforeach()
    brulion_set_warnings(brulion_core_${name})
    target_link_libraries(brulion_core INTERFACE brulion_core_${name})
endfunction()

file(GLOB _brulion_core_modules LIST_DIRECTORIES true RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)
list(SORT _brulion_core_modules)
foreach(_m IN LISTS _brulion_core_modules)
    if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${_m} AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_m}/CMakeLists.txt)
        add_subdirectory(${_m})
    endif()
endforeach()
