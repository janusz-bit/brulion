# Atrament — Brulion's own UI framework (docs/ATRAMENT.md).
#
# Each area lives in src/atrament/<area>/ with its own CMakeLists.txt calling
#   brulion_add_atrament_module(<area> SOURCES ... [DEPS <other areas>] [LIBS <other targets>])
# which makes the static library `brulion_atrament_<area>`. Areas are picked up automatically,
# in dependency order (the list below), so they never edit this file. None of them uses Qt
# (docs/ATRAMENT.md D2) except `qt` (the Qt Quick host) and `gallery` (its developer app).
#
# HarfBuzz shapes the text and gives the glyphs' outlines (D7): the vendored one, brulion_harfbuzz
# (3rdparty/harfbuzz, added by the top-level CMakeLists.txt).

set_directory_properties(PROPERTIES QT_EXCLUDE_FROM_TRANSLATION ON)

function(brulion_add_atrament_module name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPS;LIBS" ${ARGN})
    add_library(brulion_atrament_${name} STATIC ${ARG_SOURCES})
    target_include_directories(brulion_atrament_${name} PUBLIC ${PROJECT_SOURCE_DIR}/src/atrament)
    target_compile_features(brulion_atrament_${name} PUBLIC cxx_std_20)
    foreach(_dep IN LISTS ARG_DEPS)
        target_link_libraries(brulion_atrament_${name} PUBLIC brulion_atrament_${_dep})
    endforeach()
    if(ARG_LIBS)
        target_link_libraries(brulion_atrament_${name} PUBLIC ${ARG_LIBS})
    endif()
    # Qt-free: no moc, no Qt definitions, and so no precompiled Qt headers (cmake/BrulionPch.cmake
    # picks the targets with QT_NO_KEYWORDS).
    set_target_properties(brulion_atrament_${name} PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    brulion_set_warning_flags(brulion_atrament_${name})
endfunction()

foreach(_area base scene raster text layout ui qt gallery)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_area}/CMakeLists.txt)
        add_subdirectory(${_area})
    endif()
endforeach()
