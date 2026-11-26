# atrament-gallery: every Atrament element, for developing the framework (docs/ATRAMENT.md M0).
# A developer tool: not installed, not translated (its texts are sample content).
if(NOT TARGET brulion_atrament_qt)
    return()
endif()
qt_add_executable(atrament-gallery main.cpp gallery.cpp gallery.h)
target_link_libraries(atrament-gallery PRIVATE brulion_atrament_qt Qt6::Quick)
file(GLOB _atrament_gallery_fonts CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/assets/fonts/ui/*.ttf)
qt_add_resources(atrament-gallery atrament_gallery_fonts
    PREFIX /atrament/fonts/ui
    BASE ${PROJECT_SOURCE_DIR}/assets/fonts/ui
    FILES ${_atrament_gallery_fonts}
    OPTIONS --no-compress)
qt_add_resources(atrament-gallery atrament_gallery_fallback_fonts
    PREFIX /atrament/fonts
    BASE ${PROJECT_SOURCE_DIR}/assets/fonts
    FILES ${PROJECT_SOURCE_DIR}/assets/fonts/BrulionEmoji.ttf ${PROJECT_SOURCE_DIR}/assets/fonts/BrulionMath.otf
    OPTIONS --no-compress)
brulion_set_warnings(atrament-gallery)
set_target_properties(atrament-gallery PROPERTIES QT_EXCLUDE_FROM_TRANSLATION ON)
