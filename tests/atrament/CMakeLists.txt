# Tests of Atrament (docs/ATRAMENT.md "Testing"): one directory per area, picked up automatically.
# QtTest is only the runner (and QImage reads and writes the golden images); the framework
# itself never sees Qt.
foreach(_area base scene raster text layout ui qt)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_area}/CMakeLists.txt AND TARGET brulion_atrament_${_area})
        add_subdirectory(${_area})
    endif()
endforeach()
