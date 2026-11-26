# The Qt Quick host (docs/ATRAMENT.md D12): the only part of Atrament that knows Qt. Built with the
# app (Qt Quick is there).
if(NOT BRULION_BUILD_APP OR NOT TARGET Qt6::Quick)
    return()
endif()
qt_add_library(brulion_atrament_qt STATIC qthost.cpp qthost.h)
target_include_directories(brulion_atrament_qt PUBLIC ${PROJECT_SOURCE_DIR}/src/atrament/qt)
target_link_libraries(brulion_atrament_qt PUBLIC brulion_atrament_ui Qt6::Quick Qt6::Gui)
brulion_set_warnings(brulion_atrament_qt)
