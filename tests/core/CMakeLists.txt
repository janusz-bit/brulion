# One sub-directory per core module (tests/core/<module>/CMakeLists.txt), added automatically.
file(GLOB _brulion_test_dirs LIST_DIRECTORIES true RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)
list(SORT _brulion_test_dirs)
foreach(_d IN LISTS _brulion_test_dirs)
    if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${_d} AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_d}/CMakeLists.txt)
        add_subdirectory(${_d})
    endif()
endforeach()
