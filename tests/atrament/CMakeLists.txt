# Tests of Atrament (docs/ATRAMENT.md "Testing"): one directory per area, picked up automatically.
# QtTest is only the runner (and QImage reads and writes the golden images); the framework
# itself never sees Qt.
foreach(_area base scene raster text layout ui qt)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_area}/CMakeLists.txt AND TARGET brulion_atrament_${_area})
        add_subdirectory(${_area})
    endif()
endforeach()
# The block editor drawn by Atrament (src/app/editor/atrament): the app's editor library with it.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/editor/CMakeLists.txt AND TARGET brulion_editor_atrament)
    add_subdirectory(editor)
endif()
# The app's views drawn by Atrament (src/app/views/atrament).
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/views/CMakeLists.txt AND TARGET brulion_views_atrament)
    add_subdirectory(views)
endif()
