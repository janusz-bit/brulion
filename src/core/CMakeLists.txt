# brulion_core — GUI-free logic shared by every target (Linux, Android, WebAssembly).
# Every module directory with a CMakeLists.txt is added automatically; a module adds its
# sources with target_sources(brulion_core PRIVATE ...) so modules never edit this file.
qt_add_library(brulion_core STATIC types.h)
target_include_directories(brulion_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_core PUBLIC Qt6::Core Qt6::Network)
brulion_set_warnings(brulion_core)

file(GLOB _brulion_core_modules LIST_DIRECTORIES true RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)
list(SORT _brulion_core_modules)
foreach(_m IN LISTS _brulion_core_modules)
    if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${_m} AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_m}/CMakeLists.txt)
        add_subdirectory(${_m})
    endif()
endforeach()
