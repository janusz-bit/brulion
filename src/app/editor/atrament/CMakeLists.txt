# brulion_editor_atrament — the block editor drawn by Atrament (docs/ATRAMENT.md "The block editor
# in Atrament (M2)"): PageView, an Atrament view over BlockListModel, and the rendered text's spans.
# Built where Atrament is (src/app/editor/CMakeLists.txt adds it when brulion_atrament_ui exists).
qt_add_library(brulion_editor_atrament STATIC
    renderedspans.h renderedspans.cpp
    pageview.h pageview.cpp
    pageediting.cpp
)
target_include_directories(brulion_editor_atrament PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_editor_atrament PUBLIC brulion_editor brulion_atrament_ui Qt6::Core Qt6::Gui)
brulion_set_warnings(brulion_editor_atrament)
