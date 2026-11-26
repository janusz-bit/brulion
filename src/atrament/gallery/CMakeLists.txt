# atrament-gallery: every Atrament element, for developing the framework (docs/ATRAMENT.md M0).
# A developer tool: not installed, not translated (its texts are sample content).
if(NOT TARGET brulion_atrament_qt)
    return()
endif()
# The gallery's views as a library: the developer app below and the Brulion app's `--atrament`
# page (src/app/ui/atramentview.h) show them.
qt_add_library(brulion_atrament_gallery STATIC gallery.cpp gallery.h)
target_include_directories(brulion_atrament_gallery PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_atrament_gallery PUBLIC brulion_atrament_qt Qt6::Quick)
brulion_set_warnings(brulion_atrament_gallery)
set_target_properties(brulion_atrament_gallery PROPERTIES QT_EXCLUDE_FROM_TRANSLATION ON)

# The developer app: desktop only (the phone and the browser show the gallery in Brulion itself,
# `--atrament`). Its finalizer walks the host's private Qt modules: they are known here too.
if(ANDROID OR EMSCRIPTEN)
    return()
endif()
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 REQUIRED COMPONENTS GuiPrivate QuickPrivate)
qt_add_executable(atrament-gallery main.cpp)
target_link_libraries(atrament-gallery PRIVATE brulion_atrament_gallery Qt6::Quick)
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
